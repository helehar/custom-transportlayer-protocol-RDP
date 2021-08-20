# custom-transportlayer-protocol-RDP
This project was my submission for an exam in one of my software engineering classes

I have in this program created a new transport layer, RDP, which provides multiplexing and reliability, a client that retrieves files from a server, and a server that uses RDP to deliver large files to several clients reliably. I use UDP to emulate a connectionless, unreliable network layer protocol underneath the transport protocol RDP. 

to run in command line: 
open as many terminals as you want clients to recieve a file - each client gets its own terminal. 
*run makefile*
in one terminal: ./server <PORT> <filename> <number of files> <loss probability> 
in the other terminals: ./client <SERVER PORT> <Server ip adress> <loss probability>
