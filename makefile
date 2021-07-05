CFLAGS = -g -std=gnu11 -Wall -Wextra
BIN = server client
all: $(BIN)

server: server.o rdp.o send_packet.o new_fsp_server.o
	gcc $(CFLAGS) server.o rdp.o send_packet.o new_fsp_server.o -o server

client: client.o rdp.o send_packet.o
	gcc $(CLFAGS) client.o rdp.o send_packet.o -o client

server.o: server.c
	gcc $(CFLAGS) -c server.c

client.o: client.c
	gcc $(CFLAGS) -c client.c

rdp.o: rdp.c rdp.h
	gcc $(CFLAGS) -c rdp.c

send_packet.o: send_packet.c send_packet.h
	gcc $(CFLAGS) -c send_packet.c

new_fsp_server.o: new_fsp_server.c new_fsp_server.h
	gcc $(CFLAGS) -c new_fsp_server.c

clean:
	rm -f *.o $(BIN)
