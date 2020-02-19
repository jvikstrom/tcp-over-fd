#ifndef _TCP_FD_CLIENT_H_
#define _TCP_FD_CLIENT_H_
#include <stdio.h> 
#include <time.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_icmp4.h>
#include <pico_dev_tap.h>
#include <pico_socket.h>

void compare_results(pico_time __attribute__((unused)) now, void __attribute__((unused)) *arg);
void cb_tcpclient(uint16_t ev, struct pico_socket *s);

#endif