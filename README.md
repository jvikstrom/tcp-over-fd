# tcp-over-fd
Starts a TCP stack using files to do the packet sending/receiving.
The file format is: `lenght of packet (uint32) - IP packet` in binary for every packet sent.

# Build:
* First build picotcp (should be to just run make in picotcp).
* Run build.sh
