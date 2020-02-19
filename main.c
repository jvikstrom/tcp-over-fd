#include <stdio.h> 
#include <time.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_icmp4.h>
#include <pico_dev_tap.h>
#include <pico_socket.h>

#define NUM_PING 1000
#define BUF_SIZE 100000

static int finished = 0;

/* gets called when the ping receives a reply, or encounters a problem */
void cb_ping(struct pico_icmp4_stats *s)
{
    char host[30];
    pico_ipv4_to_string(host, s->dst.addr);
    if (s->err == 0) {
        /* if all is well, print some pretty info */
        printf("%lu bytes from %s: icmp_req=%lu ttl=%lu time=%lu ms\n", s->size,
                host, s->seq, s->ttl, (long unsigned int)s->time);
        if (s->seq >= NUM_PING)
            finished = 1;
    } else {
        /* if something went wrong, print it and signal we want to stop */
        printf("PING %lu to %s: Error %d\n", s->seq, host, s->err);
        finished = 1;
    }
}

FILE* driverInput;
FILE* driverOutput;
/*
    Device driver site: https://github.com/tass-belgium/picotcp/wiki/Device-Drivers
    Example device driver: https://github.com/tass-belgium/picotcp/wiki/Example-device-driver
    Pico example: https://github.com/tass-belgium/picotcp/wiki/Examples
    EOF checking: https://www.geeksforgeeks.org/eof-and-feof-in-c/
*/
static int pico_eth_send(struct pico_device *dev, void *buf, int len) {
    printf(">> pico_eth_send to: %i\n  ", len);
    fwrite(&len, sizeof(int), 1, driverOutput);
    fwrite(buf, sizeof(void), len, driverOutput);
    fflush(driverOutput);
    return len;
}

static char pollBuffer[BUF_SIZE];
static uint8_t pollBufferUint8[BUF_SIZE];

static int pico_eth_poll(struct pico_device *dev, int loop_score){
    while (loop_score > 0) {
//        printf("POLL\n");
        int len = 0;
        int nread = fread(&len, sizeof(int), 1, driverInput);
        if(!nread) {
//            printf("  EOF!\n");
            break;
        }
        fread(&pollBuffer, sizeof(void), len, driverInput);
        for(int i = 0; i < len; i++) {
            pollBufferUint8[i] = pollBuffer[i];
        }
        printf(">> pico_eth_poll: %i\n", len);
        pico_stack_recv(dev, pollBufferUint8, (uint32_t)len); /* this will copy the frame into the stack */
        loop_score--;
    }

    /* return (original_loop_score - amount_of_packets_received) */
    return loop_score;
}

struct pico_device *pico_eth_create(const char *name, const uint8_t *mac, FILE* input, FILE* output){
    /* Create device struct */
    struct pico_device* eth_dev = PICO_ZALLOC(sizeof(struct pico_device));
    if(!eth_dev) {
        return NULL;
    }
    eth_dev->send = pico_eth_send;
    eth_dev->poll = pico_eth_poll;
    driverInput = input;
    driverOutput = output;

    /* Register the device in picoTCP */
    if( 0 != pico_device_init(eth_dev, name, NULL)) {
        dbg("Device init failed.\n");
        PICO_FREE(eth_dev);
        return NULL;
    }

    /* Return a pointer to the device struct */ 
    return eth_dev;
}

/*
    All TCP server stuff is copied from an example...
*/
static int flag = 0;
#define BSIZE (1024 * 10)
static char recvbuf[BSIZE];
static int pos = 0, len = 0;

void deferred_exit(pico_time __attribute__((unused)) now, void *arg)
{
    if (arg) {
        free(arg);
        arg = NULL;
    }

    printf("%s: quitting\n", __FUNCTION__);
    exit(0);
}

int send_tcpecho(struct pico_socket *s)
{
    int w, ww = 0;
    if (len > pos) {
        do {
            w = pico_socket_write(s, recvbuf + pos, len - pos);
            if (w > 0) {
                pos += w;
                ww += w;
                if (pos >= len) {
                    pos = 0;
                    len = 0;
                }
            }
        } while((w > 0) && (pos < len));
    }

    return ww;
}
void cb_tcpecho(uint16_t ev, struct pico_socket *s) {
    int r = 0;
    printf("tcpserver> wakeup ev=%u\n", ev);

    if (ev & PICO_SOCK_EV_RD) {
        if (flag & PICO_SOCK_EV_CLOSE)
            printf("SOCKET> EV_RD, FIN RECEIVED\n");

        while (len < BSIZE) {
            r = pico_socket_read(s, recvbuf + len, BSIZE - len);
            if (r > 0) {
                len += r;
                flag &= ~(PICO_SOCK_EV_RD);
            } else {
                flag |= PICO_SOCK_EV_RD;
                break;
            }
        }
        if (flag & PICO_SOCK_EV_WR) {
            flag &= ~PICO_SOCK_EV_WR;
            send_tcpecho(s);
        }
    }

    if (ev & PICO_SOCK_EV_CONN) {
        uint32_t ka_val = 0;
        struct pico_socket *sock_a = {
            0
        };
        struct pico_ip4 orig = {
            0
        };
        uint16_t port = 0;
        char peer[30] = {
            0
        };
        int yes = 1;

        sock_a = pico_socket_accept(s, &orig, &port);
        pico_ipv4_to_string(peer, orig.addr);
        printf("Connection established with %s:%d.\n", peer, short_be(port));
        pico_socket_setoption(sock_a, PICO_TCP_NODELAY, &yes);
        /* Set keepalive options */
        ka_val = 5;
        pico_socket_setoption(sock_a, PICO_SOCKET_OPT_KEEPCNT, &ka_val);
        ka_val = 30000;
        pico_socket_setoption(sock_a, PICO_SOCKET_OPT_KEEPIDLE, &ka_val);
        ka_val = 5000;
        pico_socket_setoption(sock_a, PICO_SOCKET_OPT_KEEPINTVL, &ka_val);
    }

    if (ev & PICO_SOCK_EV_FIN) {
        printf("Socket closed. Exit normally. \n");
        if (!pico_timer_add(2000, deferred_exit, NULL)) {
            printf("Failed to start exit timer, exiting now\n");
            exit(1);
        }
    }

    if (ev & PICO_SOCK_EV_ERR) {
        printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
        exit(1);
    }

    if (ev & PICO_SOCK_EV_CLOSE) {
        printf("Socket received close from peer.\n");
        if (flag & PICO_SOCK_EV_RD) {
            pico_socket_shutdown(s, PICO_SHUT_WR);
            printf("SOCKET> Called shutdown write, ev = %d\n", ev);
        }
    }

    if (ev & PICO_SOCK_EV_WR) {
        r = send_tcpecho(s);
        if (r == 0)
            flag |= PICO_SOCK_EV_WR;
        else
            flag &= (~PICO_SOCK_EV_WR);
    }
}

/**
 * 
 * Client
 * 
 * **/
void compare_results(pico_time __attribute__((unused)) now, void __attribute__((unused)) *arg)
{
#ifdef CONSISTENCY_CHECK /* TODO: Enable */
    int i;
    printf("Calculating result.... (%p)\n", buffer1);

    if (memcmp(buffer0, buffer1, TCPSIZ) == 0)
        exit(0);

    for (i = 0; i < TCPSIZ; i++) {
        if (buffer0[i] != buffer1[i]) {
            fprintf(stderr, "Error at byte %d - %c!=%c\n", i, buffer0[i], buffer1[i]);
            exit(115);
        }
    }
#endif
    exit(0);

}

static char *buffer1;
static char *buffer0;
#define TCPSIZ (1024 * 1024 * 5)

void cb_tcpclient(uint16_t ev, struct pico_socket *s)
{
    static int w_size = 0;
    static int r_size = 0;
    static int closed = 0;
    int r, w;
    static unsigned long count = 0;

    count++;
    printf("tcpclient> wakeup %lu, event %u\n", count, ev);

    if (ev & PICO_SOCK_EV_RD) {
        do {
            r = pico_socket_read(s, buffer1 + r_size, TCPSIZ - r_size);
            if (r > 0) {
                r_size += r;
                printf("SOCKET READ - %d\n", r_size);
            }

            if (r < 0)
                exit(5);
        } while(r > 0);
    }

    if (ev & PICO_SOCK_EV_CONN) {
        printf("Connection established with server.\n");
    }

    if (ev & PICO_SOCK_EV_FIN) {
        printf("Socket closed. Exit normally. \n");
        if (!pico_timer_add(2000, compare_results, NULL)) {
            printf("Failed to start exit timer, exiting now\n");
            exit(1);
        }
    }

    if (ev & PICO_SOCK_EV_ERR) {
        printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
        exit(1);
    }

    if (ev & PICO_SOCK_EV_CLOSE) {
        printf("Socket received close from peer - Wrong case if not all client data sent!\n");
        pico_socket_close(s);
        return;
    }

    if (ev & PICO_SOCK_EV_WR) {
        if (w_size < TCPSIZ) {
            do {
                w = pico_socket_write(s, buffer0 + w_size, TCPSIZ - w_size);
                if (w > 0) {
                    w_size += w;
                    printf("SOCKET WRITTEN - %d\n", w_size);
                    if (w < 0)
                        exit(5);
                }
            } while(w > 0);
        } else {
#ifdef INFINITE_TCPTEST
            w_size = 0;
            return;
#endif
            if (!closed) {
                pico_socket_shutdown(s, PICO_SHUT_WR);
                printf("Called shutdown()\n");
                closed = 1;
            }
        }
    }
}

int main(int argc, char* argv[]){
    if(argc != 4) {
        printf("Expects at least 3 arguments: 'executable in-file out-file'\n");
        return -1;
    }
    const char *inputFname = argv[1];
    const char *outputFname = argv[2];
    const char* mode = argv[3];
    int server = mode[0] == '1';
    printf("mode: %s, mode[0]: %c, secondary: %i\n", mode, mode[0], server);

    int id;
    struct pico_ip4 ipaddr, netmask, ipremote;
    struct pico_device* dev;
    pico_stack_init();

    FILE* input = fopen(inputFname, "ab+"); // The file we read from.
    FILE* output = fopen(outputFname, "ab+"); // The file we write to.
    if(input == NULL) {
        printf("Input file could not be opened!\n");
        return -1;
    }
    if(output == NULL) {
        printf("Output file could not be opened!\n");
        return -1;
    }

    /* create the device */
    uint8_t mac = 1 + server;
    dev = pico_eth_create("mydev", &mac, input, output);
    if (!dev)
        return -1;

    char* remoteAddr = server ? "192.168.5.4" : "192.168.5.5";
    const char* thisAddr = server ? "192.168.5.5" : "192.168.5.4";
    uint16_t listen_port = server ? 1234 : 1235;
    uint16_t send_port = 1234;
    pico_string_to_ipv4(thisAddr, &ipaddr.addr);
    pico_string_to_ipv4(remoteAddr, &ipremote.addr);
    pico_string_to_ipv4("255.255.255.0", &netmask.addr);
    pico_ipv4_link_add(dev, ipaddr, netmask);

    struct pico_socket *s;
    struct pico_ip4 inaddr = {0};
    
    s = server ? pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_tcpecho) : pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_tcpclient);

    int ret = pico_socket_bind(s, &inaddr, &listen_port);
    if (ret < 0) {
        printf("%s: error binding socket to port %u: %s\n", __FUNCTION__, short_be(listen_port), strerror(pico_err));
        return -1;
    }

    if(server) {
        if (pico_socket_listen(s, 40) != 0) {
            printf("%s: error listening on port %u\n", __FUNCTION__, short_be(listen_port));
            return -1;
        }
    } else {
        if(pico_socket_connect(s, &ipremote, send_port) != 0) {
            printf("%s: error connecting to port %u\n", __FUNCTION__, short_be(send_port));
            return -1;
        }
    }

    while(1) {
        pico_stack_tick();
        usleep(2000);
    }


    /* assign the IP address to the tap interface */
/*    char* pingTo = secondary ? "192.168.5.4" : "192.168.5.5";
    const char* thisIP = secondary ? "192.168.5.5" : "192.168.5.4";
    pico_string_to_ipv4(thisIP, &ipaddr.addr);
    pico_string_to_ipv4("255.255.255.0", &netmask.addr);
    pico_ipv4_link_add(dev, ipaddr, netmask);

    if(!secondary) {
        printf("starting ping\n");
        id = pico_icmp4_ping(pingTo, NUM_PING, 1000, 10000, 64, cb_ping);
    }

    if (id == -1)
        return -1;
*/
    /* keep running stack ticks to have picoTCP do its network magic. Note that
     * you can do other stuff here as well, or sleep a little. This will impact
     * your network performance, but everything should keep working (provided
     * you don't go overboard with the delays). */
/*    while (finished != 1)
    {
        usleep(1000);
        pico_stack_tick();
    }
*/
    printf("finished !\n");
    return 0;
}
