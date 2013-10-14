#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "common.h"
#include "err.h"
#include "midi.h"
#include "sds.h"

// Size of the buffer for reading midi data.
#define READ_RESPONSE_BUF_SIZE 20

#define __TRACE_GET_RESPONSE 0
#define __DEBUG_GET_RESPONSE 1
#define __TRACE_SEND_PACKETS 0



typedef enum {
    STATE0,  /* seen [],             hoping for f0             */
    STATE1,  /* seen [f0]            hoping for 7e             */
    STATE2,  /* seen [f0,7e]         hoping for channel number */
    STATE3,  /* seen [f0,7e,CN]      hoping for 7{c,d,e,f}     */
    STATE4,  /* seen [f0,7e,CN,x]    hoping for packet number  */
    STATE5,  /* seen [f0,7e,CN,x,PN] hoping for f7             */
} response_state_t;

static void
display_usage(void);

static int
send_file(
    int fd,
    size_t file_size,
    midi_t midi,
    unsigned int channel_num,
    unsigned int sample_num,
    err_t err
);

static int
get_response(
    midi_t midi,
    unsigned int channel_num,
    unsigned int modded_packet_num,
    response_t *response
);



int main(int argc, char **argv)
{
    int ret;
    char *device, *channel_string, *sample_string, *filename;
    unsigned int channel_num, sample_num;
    int fd;
    size_t file_size;
    err_t err;
    midi_t midi;

    ret            = 1;
    device         = NULL;
    filename       = NULL;
    channel_string = NULL;
    sample_string  = NULL;
    channel_num    = 0;
    fd             = 0;
    err            = err_create(256);
    midi           = NULL;

    if (argc != 1+4) {
        display_usage();
        goto end;
    }

    device         = argv[1];
    channel_string = argv[2];
    sample_string  = argv[3];
    filename       = argv[4];

    if (!convert_channel_num(channel_string, &channel_num, err) ||
        !convert_sample_num(sample_string, &sample_num, err)    ||
        !midi_open_interface(device, &midi, err)                ||
        !sds_open_file(filename, &fd, err)                      ||
        !sds_get_file_size(fd, &file_size, err)                 ||
        !sds_file_size_is_ok(file_size, err)                    ||
        !send_file(fd, file_size, midi, channel_num, sample_num, err)) {

        fprintf(stderr, "%s\n", err_get(err));
        goto end;
    }

    ret = 0;

end:
    err_destroy(err);

    if (fd) {
        close(fd);
    }

    midi_close_interface(midi);

    return ret;
}



static void
display_usage(void)
{
    fprintf(
        stderr,
        "send-sds " VERSION "\n"
        "usage: <alsa-device> <channel-num> <sample-num> <sds-filename>\n"
    );
}

static int
send_file(
    int fd,
    size_t file_size,
    midi_t midi,
    unsigned int channel_num,
    unsigned int sample_num,
    err_t err
) {
    const char *indent = "    ";
    const char *trace = "[TRACE]";

    unsigned char buf[max(SDS_HEADER_LENGTH, SDS_PACKET_LENGTH)];
    response_t response = RESPONSE_NULL;
    unsigned int num_packets, packet_num, modded_packet_num;

    if (!sds_read_header(fd, buf, sizeof(buf), err)) {
        return 0;
    }

    /* patch in channel number */
    buf[2] = (unsigned char) channel_num;

    /* patch in sample number */
    buf[4] = sample_num & 0x7f;
    buf[5] = (sample_num >> 7) & 0x7f;

    char dump_header_str[60]; dump_header_str[0] = '\0';
    if (!midi_send(midi, buf, SDS_HEADER_LENGTH, err)) {
        return 0;
    } else {
        sds_serialize_header(dump_header_str, buf);
        printf("Sent Dump Header: %s\n", dump_header_str);
    }

    if (! get_response(midi, channel_num, 0, &response)) {
        fprintf(stderr, "could not get response");
        return 0;
    } else {
        printf("Received %s\n", response_to_string(response));
    }

    while (response != RESPONSE_ACK) {
        if (! get_response(midi, channel_num, 0, &response)) {
            fprintf(stderr, "could not get response");
            return 0;
        } else {
            printf("%sReceived %s\n", indent, response_to_string(response));
        }
    }

    num_packets = sds_calc_num_packets(file_size);

    char packet_str[100];
    for (packet_num=0; packet_num < num_packets; ) {
        modded_packet_num = packet_num % 0x80;
        packet_str[0] = '\0';

        printf("Packet %d\n", modded_packet_num);

        if (__TRACE_SEND_PACKETS) {
            printf("%s %s reading packet %d\n",
                   trace, __FUNCTION__, packet_num);
        }

        if (!sds_read_packet(fd, buf, sizeof(buf), err)) {
            return 0;
        }

        if (__TRACE_SEND_PACKETS) {
            printf("%s %s done reading packet %d\n",
                   trace, __FUNCTION__, packet_num);
        }

        /* XXX patch channel number */
        /* XXX patch packet number */

        if (__TRACE_SEND_PACKETS) {
            printf("%s %s sending packet %d\n",
                   trace, __FUNCTION__, packet_num);
        }
        if (!midi_send(midi, buf, SDS_PACKET_LENGTH, err)) {
            return 0;
        } else {
            sds_serialize_packet(packet_str, buf, SDS_PACKET_LENGTH);
            printf("%sSent %s\n", indent, packet_str);
        }

        if (__TRACE_SEND_PACKETS) {
            printf("%s %s done sending packet %d\n",
                   trace, __FUNCTION__, packet_num);
        }

        if (!get_response(midi, channel_num, modded_packet_num, &response)) {
            fprintf(stderr,
                    "get_response failed with response %s\n",
                    response_to_string(response));
            return 0;
        } else {
            printf("%sReceived %s\n", indent, response_to_string(response));
        }

        if (response != RESPONSE_ACK) {
            fprintf(stderr,
                    "received %s instead of %s in response to packet %d",
                    response_to_string(response),
                    response_to_string(RESPONSE_ACK),
                    packet_num);
            return 0;
        } else {
            if (__TRACE_SEND_PACKETS) {
                printf("%s %s received response %s from sending packet %d\n",
                       trace, __FUNCTION__, response_to_string(response), packet_num);
            }
        }

        packet_num++;
    }

    return 1;
}

static int
get_response(midi_t midi,
             unsigned int channel_num,
             unsigned int modded_packet_num,
             response_t *response) {
    const time_t start_time = time(NULL);
    const time_t timeout_sec = 2;

    int bytes_read;
    time_t now;
    unsigned char c, t;

    bytes_read = 0;

    while (bytes_read < SDS_RESPONSE_LENGTH) {
        now = time(NULL);

        if (!midi_read(midi, &c)) {
            return 0;
        }

        if (now - start_time > timeout_sec) {
            *response = RESPONSE_TIMEOUT;
            return 0;
        }

        switch (bytes_read) {
        case 0:
            if (c == 0xf0) {
                bytes_read++;
            }
            break;

        case 1:
            if (c == 0x7e) {
                bytes_read++;
            }
            break;

        case 2:
            if (c == channel_num) {
                bytes_read++;
            }                       
            break;

        case 3:
            if (c >= 0x7c && c <= 0x7f) {
                t = c;
                bytes_read++;
            }
            break;

        case 4:
            if (c == modded_packet_num) {
                bytes_read++;
            }
            break;

        case 5:
            if (c == 0xf7) {
                switch (t) {
                case 0x7c:
                    *response = RESPONSE_WAIT;
                    break;
                case 0x7d:
                    *response = RESPONSE_CANCEL;
                    break;
                case 0x7e:
                    *response = RESPONSE_NAK;
                    break;
                case 0x7f:
                    *response = RESPONSE_ACK;
                    break;
                }

                return 1;
            }
            break;
        }
    }

    return 0;
}
