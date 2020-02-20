#include "server.h"

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

int start_server(struct pico_socket* s, uint16_t *listen_port) {
    s = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_tcpecho);
    struct pico_ip4 inaddr = {0};
    int ret = pico_socket_bind(s, &inaddr, listen_port);
    if (ret < 0) {
        printf("%s: error binding socket to port %u: %s\n", __FUNCTION__, short_be(*listen_port), strerror(pico_err));
        return ret;
    }
    ret = pico_socket_listen(s, 40);
    if(ret != 0) {
        printf("%s: error listening on port %u\n", __FUNCTION__, short_be(*listen_port));
        return ret;
    }
    return ret;
}
