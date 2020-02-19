#!/bin/bash
#gcc -c -g -o main.o -I./picotcp/build/include main.c client.c server.c
#gcc -g -o main.elf main.o ./picotcp/build/lib/libpicotcp.a

gcc -g -o main.o -I./picotcp/build/include main.c client.c server.c ./picotcp/build/lib/libpicotcp.a
