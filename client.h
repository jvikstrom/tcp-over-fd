#ifndef _TCP_FD_CLIENT_H_
#define _TCP_FD_CLIENT_H_
#include <stdio.h> 
#include <time.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_icmp4.h>
#include <pico_dev_tap.h>
#include <pico_socket.h>

int connect_client(struct pico_socket *s, struct pico_ip4* remote, uint16_t remote_port, uint16_t *listen_port);

void compare_results(pico_time __attribute__((unused)) now, void __attribute__((unused)) *arg);
void cb_tcpclient(uint16_t ev, struct pico_socket *s);

#endif