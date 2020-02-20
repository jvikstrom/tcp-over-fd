#include <stdio.h> 
#include <time.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_icmp4.h>
#include <pico_dev_tap.h>
#include <pico_socket.h>
#include "server.h"
#include "client.h"

#define BUF_SIZE 100000

static int finished = 0;

FILE* driverInput;
FILE* driverOutput;
static int pico_eth_send(struct pico_device *dev, void *buf, int len) {
    printf(">> pico_eth_send: %i\n", len);
    uint32_t len32 = len;
    fwrite(&len32, sizeof(int32_t), 1, driverOutput);
    fwrite(buf, sizeof(void), len, driverOutput);
    fflush(driverOutput);
    return len;
}

static char pollBuffer[BUF_SIZE];
static uint8_t pollBufferUint8[BUF_SIZE];

static int pico_eth_poll(struct pico_device *dev, int loop_score){
    while (loop_score > 0) {
        uint32_t len = 0;
        int nread = fread(&len, sizeof(len), 1, driverInput);
        if(!nread) {
            break;
        }
        fread(&pollBuffer, sizeof(void), len, driverInput);
        for(int i = 0; i < len; i++) {
            pollBufferUint8[i] = pollBuffer[i];
        }
        printf(">> pico_eth_poll: %i\n", len);
        pico_stack_recv(dev, pollBufferUint8, len); /* this will copy the frame into the stack */
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

    //char* remoteAddr = server ? "192.168.5.4" : "192.168.5.5";
    char* remoteAddr = "192.168.5.5";
    const char* thisAddr = server ? "192.168.5.5" : "192.168.5.4";
    uint16_t listen_port = server ? 1234 : 1235;
    uint16_t remote_port = 1234;
    pico_string_to_ipv4(thisAddr, &ipaddr.addr);
    pico_string_to_ipv4(remoteAddr, &ipremote.addr);
    pico_string_to_ipv4("255.255.255.0", &netmask.addr);
    pico_ipv4_link_add(dev, ipaddr, netmask);

    struct pico_socket s;
    struct pico_ip4 inaddr = {0};
    int ret;
    if(server) {
        printf("Starting server on port: %i\n", listen_port);
        ret = start_server(&s, &listen_port);
    } else {
        printf("Starting client and connecting to %s:%i\n", remoteAddr, remote_port);
        ret = connect_client(&s, &ipremote, remote_port, &listen_port);
    }
    if (ret < 0) {
        printf("Error starting server or client...\n");
        return -1;
    }

    while(1) {
        pico_stack_tick();
        usleep(2000);
    }
    printf("finished !\n");
    return 0;
}
