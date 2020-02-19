#ifndef _FD_SERVER_H_
#define _FD_SERVER_H_
#include <stdio.h> 
#include <time.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_icmp4.h>
#include <pico_dev_tap.h>
#include <pico_socket.h>

void deferred_exit(pico_time __attribute__((unused)) now, void *arg);

int send_tcpecho(struct pico_socket *s);

void cb_tcpecho(uint16_t ev, struct pico_socket *s);

#endif