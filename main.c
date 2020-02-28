#include <stdio.h> 
#include <time.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_icmp4.h>
#include <pico_dev_tap.h>
#include <pico_socket.h>
#include <unistd.h>  
#include "server.h"
#include "client.h"

#define BUF_SIZE 100000

typedef struct pcap_hdr_s {
    uint32_t magic_number;   /* magic number */
    uint16_t version_major;  /* major version number */
    uint16_t version_minor;  /* minor version number */
    int32_t  thiszone;       /* GMT to local correction */
    uint32_t sigfigs;        /* accuracy of timestamps */
    uint32_t snaplen;        /* max length of captured packets, in octets */
    uint32_t network;        /* data link type */
} pcap_hdr_t;

void writePCAPHeader(FILE * f) {
    pcap_hdr_t hdr;
    hdr.magic_number = 0xa1b2c3d4;
    hdr.version_major = 2;
    hdr.version_minor = 4;
    hdr.thiszone = 0;
    hdr.sigfigs = 0;
    hdr.snaplen = 65535;
    hdr.network = 101; // LINKTYPE_RAW;; There is also LINKTYPE_IPV4/LINKTYPE_IPV6,.

    fwrite(&hdr.magic_number, sizeof(hdr.magic_number), 1, f);
    fwrite(&hdr.version_major, sizeof(hdr.version_major), 1, f);
    fwrite(&hdr.version_minor, sizeof(hdr.version_minor), 1, f);
    fwrite(&hdr.thiszone, sizeof(hdr.thiszone), 1, f);
    fwrite(&hdr.sigfigs, sizeof(hdr.sigfigs), 1, f);
    fwrite(&hdr.snaplen, sizeof(hdr.snaplen), 1, f);
    fwrite(&hdr.network, sizeof(hdr.network), 1, f);
    fflush(f);
}

// Assumes that the magic number has already been read.
void consumePCAPHeader(FILE *f) {
    pcap_hdr_t hdr;
    int nread = fread(&hdr.version_major, sizeof(hdr.version_major), 1, f);
    if(nread <= 0) {
        exit(-1);
    }
    nread = fread(&hdr.version_minor, sizeof(hdr.version_minor), 1, f);
    if(nread <= 0) {
        exit(-1);
    }
    nread = fread(&hdr.thiszone, sizeof(hdr.thiszone), 1, f);
    if(nread <= 0) {
        exit(-1);
    }
    nread = fread(&hdr.sigfigs, sizeof(hdr.sigfigs), 1, f);
    if(nread <= 0) {
        exit(-1);
    }
    nread = fread(&hdr.snaplen, sizeof(hdr.snaplen), 1, f);
    if(nread <= 0) {
        exit(-1);
    }
    nread = fread(&hdr.network, sizeof(hdr.network), 1, f);
    if(nread <= 0) {
        exit(-1);
    }
}

static int finished = 0;

typedef struct pcaprec_hdr_s {
    uint32_t ts_sec;         /* timestamp seconds */
    uint32_t ts_usec;        /* timestamp microseconds */
    uint32_t incl_len;       /* number of octets of packet saved in file */
    uint32_t orig_len;       /* actual length of packet */
} pcaprec_hdr_t;

FILE* driverInput;
uint32_t consumedPCAP = 0;
FILE* driverOutput;
int seqClock = 0;
static int pico_eth_send(struct pico_device *dev, void *buf, int len) {
    printf(">> pico_eth_send: %i\n", len);
    // Write the PCAP.
    pcaprec_hdr_t hdr;
    hdr.ts_sec = clock();
    hdr.ts_usec = seqClock++; // FIXME: Fix the timestamps.
    hdr.incl_len = len;
    hdr.orig_len = len; // FIXME: Should this be different?
    fwrite(&hdr, sizeof(hdr), 1, driverOutput);
    fwrite(buf, sizeof(void), len, driverOutput);
    fflush(driverOutput);

    return len;
}

static char pollBuffer[BUF_SIZE];
static uint8_t pollBufferUint8[BUF_SIZE];

// Assumes ts_sec is already consumed.
int read_pcap_hdr(FILE* f, pcaprec_hdr_t *hdr) {
    int nread = fread(&hdr->ts_usec, sizeof(hdr->ts_sec), 1, f);
    if(!nread) {
        return 0;
    }
    nread = fread(&hdr->incl_len, sizeof(hdr->incl_len), 1, f);
    if(!nread) {
        return 0;
    }
    nread = fread(&hdr->orig_len, sizeof(hdr->orig_len), 1, f);
    if(!nread) {
        return 0;
    }
    return 1;
}

static int pico_eth_poll(struct pico_device *dev, int loop_score){
    while (loop_score > 0) {
        uint32_t len = 0;
        int nread = fread(&len, sizeof(len), 1, driverInput);
        if(!nread) {
            break;
        }
        if(!consumedPCAP) {
            // We first need to read the header (which we can now.)
            consumePCAPHeader(driverInput);
            consumedPCAP = 1;
            nread = fread(&len, sizeof(len), 1, driverInput);
            if(!nread)
                break;
        }

        // Now read the pcap packet header.
        pcaprec_hdr_t hdr;
        hdr.ts_sec = len;
        nread = read_pcap_hdr(driverInput, &hdr);
        if(!nread) {
            printf("Something went terribly wrong!\n");
            exit(-1);
        }

        fread(&pollBuffer, sizeof(void), hdr.incl_len, driverInput);
        for(int i = 0; i < hdr.incl_len; i++) {
            pollBufferUint8[i] = pollBuffer[i];
        }
        printf(">> pico_eth_poll: %i\n", hdr.incl_len);
        pico_stack_recv(dev, pollBufferUint8, hdr.incl_len); /* this will copy the frame into the stack */
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
    char *inputFname = NULL;
    char *outputFname = NULL;
    char *thisIP = NULL;
    char *remoteIP = NULL;
    int opt;
    while((opt = getopt(argc, argv, ":t:r:hi:o:" )) != -1){
        switch(opt) {
            case 'i':
                inputFname = optarg;
                break;
            case 'o':
                outputFname = optarg;
                break;
            case 't':
                thisIP = optarg;
                break;
            case 'r':
                remoteIP = optarg;
                break;
            case 'h':
                printf("-i: the input file name\n-o: the output file name\n-t: the ip this node will use\n-r: the ip the remote node has (will start this node as a client)\n");
                return 0;
            case ':':
                printf("COLON??\n");
            case '?':
                printf("QUESTIONMARK?? %i\n", optopt);

        }
    }

    if(!inputFname || !outputFname || !thisIP) {
        printf("Must set -t, -i and -o\n");
        return -1;
    }

    printf("Starting with input file: %s, output file: %s, ip: %s, remote: %s\n", inputFname, outputFname, thisIP, remoteIP);
    int server = !remoteIP;

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
    writePCAPHeader(driverOutput);

    uint16_t listen_port = server ? 1234 : 1235;
    uint16_t remote_port = 1234;
    pico_string_to_ipv4(thisIP, &ipaddr.addr);
    pico_string_to_ipv4(remoteIP, &ipremote.addr);
    pico_string_to_ipv4("255.255.255.0", &netmask.addr);
    pico_ipv4_link_add(dev, ipaddr, netmask);

    struct pico_socket s;
    struct pico_ip4 inaddr = {0};
    int ret;
    if(server) {
        printf("Starting server on port: %i\n", listen_port);
        ret = start_server(&s, &listen_port);
    } else {
        printf("Starting client and connecting to %s:%i\n", remoteIP, remote_port);
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
