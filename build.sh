gcc -c -g -o main.o -I./picotcp/build/include main.c
gcc -g -o main.elf main.o ./picotcp/build/lib/libpicotcp.a
