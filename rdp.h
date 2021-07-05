#include "send_packet.h"

#ifndef RDP_H
#define RDP_H

#define SYN      0x01
#define FIN      0x02
#define PAYLOAD  0x04
#define ACK      0x08
#define ACCEPT   0x10
#define DENIED   0x20

/* metadata */
#define FULL   503
#define EXISTS 421


/* pakke som sendes fra rdp opp til applikasjonslaget
 * dette er pakken structen som returneres til klienten i
 * rdp_read()  */
struct rdp_app_layer_packet{
  int size;
  int payload_number;
  char payload[];
};

/* lager informasjon som er nødvendig for en rdp-kobling */
struct rdp_connection{
  struct rdp_connection *next;
  struct rdp_connection *prev;
  struct sockaddr *dest_addr;

  int client_id;
  int server_id;
  int payload_number;
  char pktseq;
};

/* klienter som ønsker å koble seg til en server
* legges i denne køen */
struct rdp_connection_queue{
  struct rdp_connection_queue *next_in_line;
  struct rdp_packet *pckt;
  struct sockaddr *dest_addr;
};


struct rdp_packet {
  unsigned char flag;
  unsigned char pktseq;
  unsigned char ackseq;
  unsigned char unassigned;
  int senderid;
  int recvid;
  int metadata;
  char payload[];
} __attribute__((packed));



/* Client */
struct rdp_app_layer_packet * rdp_read(struct rdp_connection *connection, int payload_bytes);
struct rdp_connection *rdp_connect(char * srv_ip, int client_id, unsigned short port);
void rdp_close();


/* Server */
void rdp_free_connections();
int rdp_multiplex();
void rdp_remove_connection(struct rdp_connection *connection);
int rdp_write(struct rdp_connection *cnct, char data[], int payload_bytes);
void rdp_request_reject(struct rdp_connection *con_request, int server_id, int metadata);
void rdp_request_accept(struct rdp_connection *new_connection, int server_id);
struct rdp_connection *rdp_connection_by_id(int client_id);
struct rdp_connection *rdp_accept(int connections);
void rdp_bind_port(unsigned short port);


#endif
