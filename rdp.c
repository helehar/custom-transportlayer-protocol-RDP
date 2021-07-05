#include "rdp.h"

/* Brukes i overføring av payload pakker
 * er en del av stop-and-wait protokollen */
unsigned char pktseq = 1, ackseq = 1;

/* Den dynamiske allokerte datastrukturen som
 * representerer en rdp forbindelse */
struct rdp_connection *head, *tail;

/* kommende forbindelser legges i en kø, dette inngår i
 * muktipleksingen i RDP protokollen  */
struct rdp_connection_queue *queue_head;
int queue_count = 0;

/* UDP socketen som all kommunikasjonen skal foregå gjennom */
int rdp_socket;
/* disse settes til hver klient og til serveren
 * client_addr lagres i hver unike forbindelse mellom
 * klient og server */
struct sockaddr_in server_addr, client_addr;
struct in_addr server_ip;
socklen_t addr_len;


struct rdp_packet *new_packet(
  unsigned char flag,
  unsigned char pktseq,
  unsigned char ackseq,

  int senderid,
  int recvid,

  int payload_bytes
){

  struct rdp_packet *rdp = malloc(sizeof(struct rdp_packet) + payload_bytes);
  if (rdp == NULL) {
    fprintf(stderr, "malloc failes. Possibly out of memory\n");
    rdp_free_connections();
    free(rdp);
  }
  rdp -> flag = flag;
  rdp -> pktseq = pktseq;
  rdp -> ackseq = ackseq;
  rdp -> unassigned = 0;

  rdp -> senderid = htons(senderid);
  rdp -> recvid = htons(recvid);
  return rdp;

}

/* kode fra cebra
* rdp- error checking */
void check_error(int res, char *msg) {
  if (res == -1) {
    perror(msg);
    rdp_free_connections();
    exit(EXIT_FAILURE);
  }
}

/* metoder til den dynamiske allokerte datastrukturen: */
/* kode hentet fra cebra */
struct rdp_connection *rdp_connection_by_id(int client_id){
  struct rdp_connection *temp = head->next;
  while (temp != tail){
    if ((temp->client_id) == client_id){
      return temp;
    }
    temp = temp->next;
  }
  return NULL;
}
/* kode hentet fra cebra */
void rdp_free_connections() {
  struct rdp_connection *next, *temp = head -> next;
  while (temp != tail){
    next = temp ->next;
    free(temp);
    temp = next;
  }
  if (queue_count >0){
    struct rdp_connection_queue *temp_2;
    while (queue_head != NULL){
      temp_2 = queue_head;
      queue_head = queue_head->next_in_line;
      free(temp_2);
    }
  }
  free(head);
  free(tail);
  free(queue_head);
  close(rdp_socket);
}
/* kode hentet fra cebra */
void add_connection_to_rdp(struct rdp_connection *left, struct rdp_connection *middle, struct rdp_connection *right){
  left->next = middle;
  right->prev = middle;
  middle->next = right;
  middle->prev = left;
}
/* kode hentet fra cebra */
void rdp_remove_connection(struct rdp_connection *connection){
  connection->prev->next = connection->next;
  connection->next->prev = connection->prev;
  queue_count--;
  free(connection);

}
/* kode hentet fra cebra */
void rdp_set_default_values(){
  head = malloc(sizeof(struct rdp_connection));
  tail = malloc(sizeof(struct rdp_connection));
  head -> next = tail;
  tail -> prev = head;

  queue_head = malloc(sizeof(struct rdp_connection_queue));
  queue_head -> next_in_line = NULL;
}





/* rdp metoder til CLIENT */
void rdp_send_fin(struct rdp_connection *connection){
  int wc;
  struct rdp_packet *fin_packet = new_packet(FIN,
                                            pktseq,
                                            ackseq,
                                            connection->client_id,
                                            connection->server_id,
                                            0
                                          );
  wc = send_packet(
                  rdp_socket,
                  (char*)fin_packet,
                  sizeof(fin_packet),
                  0,
                  (struct sockaddr*)&server_addr,
                  sizeof(struct sockaddr_in));

  check_error(wc, "sendto");
  free(fin_packet);
}


void rdp_close(struct rdp_connection *connection){
  rdp_send_fin(connection);
  close(rdp_socket);

}

void rdp_send_ack(struct rdp_connection *connection, struct rdp_packet *payload_packet){
  int wc;

  struct rdp_packet *ack_packet = new_packet(ACK,
                                            payload_packet->pktseq,
                                            payload_packet->ackseq,
                                            connection->client_id,
                                            connection->server_id,
                                          0);

  wc = send_packet(
    rdp_socket,
    (char*)ack_packet,
    sizeof(ack_packet),
    0,
    (struct sockaddr*)&server_addr,
    sizeof(struct sockaddr_in));

  check_error(wc, "sendto");
  free(ack_packet);

}

void rdp_bind_client_port(char * srv_ip,  unsigned short port){
  int rc;
  rdp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  check_error(rdp_socket, "socket");

  rc = inet_pton(AF_INET, srv_ip, &server_ip.s_addr);
  check_error(rc, "inet_pton");
  if (rc == 0){
    fprintf(stderr, "IP address not valid: %s\n", srv_ip);
    exit(EXIT_FAILURE);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = port;
  server_addr.sin_addr = server_ip;

}

/* applikasjonslags pakke som returneres fra RDP transportlag opp til newFSP applikasjonen - klienten */
struct rdp_app_layer_packet * rdp_read(struct rdp_connection *connection, int payload_bytes){
  int rc;
  struct rdp_packet *payload_packet = malloc(sizeof(struct rdp_packet) + payload_bytes);

  rc = recv(rdp_socket, payload_packet, sizeof(struct rdp_packet) + payload_bytes, 0);
  check_error(rc, "recv");

  /* klienten må sjekke om pakken er til seg - inngår i RDP multipleksing */
  if (htons(payload_packet->recvid) == connection->client_id){
    /* klienten må sjekke om pakken er gått tapt og blitt sendt fra server på nytt
     * inngår i RDP stop_and_wait */
    if (ackseq == payload_packet->pktseq ){
      rdp_send_ack(connection, payload_packet);
      free(payload_packet);
      return NULL;
    } else {

      struct rdp_app_layer_packet *usr_pckt = malloc((htons(payload_packet->metadata) - sizeof(struct rdp_packet)) + sizeof(struct rdp_app_layer_packet));
      memcpy(usr_pckt->payload, payload_packet->payload, htons(payload_packet->metadata) - sizeof(struct rdp_packet));
      if (usr_pckt == NULL){
        perror("strdup");
        free(payload_packet);
        free(connection);
        exit(EXIT_FAILURE);
      }
      usr_pckt->size = htons(payload_packet->metadata) - sizeof(struct rdp_packet);

      /* klienten sender akk og setter så acknummer til å være sekvensnummer
       * inngår i RDP stop_and_wait */
      rdp_send_ack(connection, payload_packet);
      ackseq = payload_packet->pktseq;
      connection->payload_number++;
      free(payload_packet);

      /* returnerer applikasjonslagspakken til klienten */
      return usr_pckt;
    }
  } else {
    /* koden kommer hit også dersom det mottas en pakke som ikke er payload pakke  */
    return NULL;
  }

}

void rdp_connection_request(int client_id){
  int wc;
  struct rdp_packet *con_request = new_packet(SYN, 0, 0, client_id, 0, 0);

  wc = send_packet(
    rdp_socket,
    (char*)con_request,
    sizeof(con_request),
    0,
    (struct sockaddr*)&server_addr,
    sizeof(struct sockaddr_in)
  );
  check_error(wc, "sendto");
  free(con_request);
}

/* blir kalt når når en klient vil koble seg til server gjennom RDP */
struct rdp_connection *rdp_connect(char * srv_ip, int client_id, unsigned short port){
  int rc;
  /* binder porten og setter en UDP adresse til klienten */
  rdp_bind_client_port(srv_ip, port);
  rdp_connection_request(client_id);

  /* for å allokere nok plass i minnet bruker jeg en av metadataene definert i rdp.h */

  struct rdp_packet *rqst_answer = malloc(sizeof(struct rdp_packet));
  rc = recv(rdp_socket, rqst_answer, (sizeof(struct rdp_packet)), 0);

  check_error(rc, "recv");

  struct rdp_connection *new_connection = malloc(sizeof(struct rdp_connection));
  if (rqst_answer->flag == ACCEPT){
    new_connection->client_id = client_id;
    new_connection->server_id = htons(rqst_answer->senderid);
    new_connection->payload_number = 0;

    free(rqst_answer);
    return new_connection;
  } else if (rqst_answer->flag == DENIED) {
    /* hvis en forespørsel returnerer flagget DENIED vil RDP gi en feilmelding
     * i form av metadata enten 503 eller 421 - definert i rdp.h */
    printf("%d: ", htons(rqst_answer->metadata));
    free(new_connection);
    free(rqst_answer);
    return NULL;
  } else {
    /* kaster pakken hvis det enten er en payload pakke - det skal ikke klienten ha, ettersom
     * kommer den hit, er den ikke akseptert til en rdp forbindelse */
    free(new_connection);
    free(rqst_answer);
    return NULL;
  }

}


/* rdp metoder til SERVER */
void rdp_set_sequence(struct rdp_connection *cnct){
  /* Inngår i rdp stop_and_wait */
  if (cnct-> pktseq == 0){
    cnct-> pktseq = 1;
  } else {
    cnct-> pktseq = 0;
  }
}

struct rdp_packet *create_payload_packet(struct rdp_connection *cnct, char data[], int payload_bytes){
  struct rdp_packet *payload_packet = new_packet(PAYLOAD, cnct->pktseq, ackseq, cnct->server_id, cnct->client_id, payload_bytes);
  payload_packet->metadata = htons(sizeof( struct rdp_packet) + payload_bytes);
  memcpy(payload_packet->payload, data, payload_bytes);

  if (payload_packet->payload == NULL){
    perror("strdup");
    free(payload_packet);
    rdp_free_connections();
    exit(EXIT_FAILURE);
  }

  return payload_packet;
}

struct rdp_packet *rdp_recieve_packet(){
  int rc;
  struct rdp_packet *packet = malloc(sizeof(struct rdp_packet));
  rc = recvfrom(rdp_socket,
            packet,
            sizeof(packet),
            0,
            (struct sockaddr *)&client_addr,
            &addr_len);
  check_error(rc, "recvfrom");

  return packet;
}

/* Denne blir kalt i rdp_multiplex og i rdp_accept hvis serveren får en pakke som ikke er SYN
* er pakken enten en FIN eller ACK
* implementasjonsvalg av hensyn til rdp multipleksing og abstrkasjonsbarriere mellom RDP og applikasjonslaget */
int check_packet(struct rdp_packet *packet){
  // finner riktig forbindelse
  struct rdp_connection *connection = rdp_connection_by_id(htons(packet->senderid));
  if (packet->flag == ACK){
    // hvis pakken er en ack, og sendt til riktig forbindelse må dette lagres i forbindelsen
    /* inngår i rdp stop_and_wait */
    if (packet -> pktseq == connection->pktseq){
      rdp_set_sequence(connection);
      connection -> payload_number++;
    }
    return 1;
  }
  else if (packet ->flag == FIN){
    rdp_remove_connection(connection);
    return 2;
  }
  else if(packet ->flag == SYN){
    /* blir returnert til applikasjonslaget, slik at server kan fjerne forbindelser lagret i sitt minneområdet */
    return 3;
  }
  return 0;

}

/* inngår i rdp multipleksing - blir kalt av applikasjonen i "hovedløkka" */
int rdp_multiplex(){
  int rc;
  fd_set stop_and_wait;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO(&stop_and_wait);
  FD_SET(rdp_socket, &stop_and_wait);

  rc = select(FD_SETSIZE, &stop_and_wait, NULL, NULL,  &tv);
  check_error(rc, "select");
  if (FD_ISSET(rdp_socket, &stop_and_wait)){
    int rc;
    struct rdp_packet *packet = malloc(sizeof(struct rdp_packet));
    rc = recvfrom(rdp_socket,
              packet,
              sizeof(packet),
              0,
              (struct sockaddr *)&client_addr,
              &addr_len);
    check_error(rc, "recvfrom");

    if (packet->flag == SYN){
      /* hvis pakken er en forbindelsesforespørsel, legges den i kø slik at neste gang applikasjonen kaller
       * rdp_accept leser den fra køen - inngår i rdp multipleksing */
      struct rdp_connection_queue *new_item_in_line = malloc(sizeof(struct rdp_connection_queue) + addr_len);
      new_item_in_line->pckt = packet;
      memcpy(&new_item_in_line->dest_addr, &client_addr, addr_len);

      struct rdp_connection_queue *temp = queue_head -> next_in_line;
      new_item_in_line->next_in_line = temp;
      queue_head -> next_in_line = new_item_in_line;
      queue_count++;
      rc = check_packet(packet);
      return rc;
    } else {
      /* er ikke pakken en SYN er det enten en FIN eller ACK, som blir håndtert i check_packet av rdp */
      rc = check_packet(packet);
      free(packet);
      return rc;

    }
  }
  return 0;
}

/* navnet sier seg selv - inngår i rdp stop_and_wait */
int stop_and_wait_protocol(){
  int rc;
  fd_set stop_and_wait;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;

  FD_ZERO(&stop_and_wait);
  FD_SET(rdp_socket, &stop_and_wait);

  rc = select(FD_SETSIZE, &stop_and_wait, NULL, NULL,  &tv);
  check_error(rc, "select");
  return rc;
}

void send_rdp_packet(struct rdp_packet *payload_packet, struct rdp_connection *connection){
  int wc;
  wc = send_packet(rdp_socket,
              (char*)payload_packet,
              htons(payload_packet->metadata),
              0,
              (struct sockaddr*)&connection->dest_addr,
              sizeof(struct sockaddr_in));
  check_error(wc, "sendto");
  free(payload_packet);

}

int rdp_write(struct rdp_connection *cnct, char data[], int payload_bytes){
  struct rdp_packet *payload_packet = create_payload_packet(cnct, data, payload_bytes);

  send_rdp_packet(payload_packet, cnct);
  /* returnerer 0 hvis ingen svar */
  if (stop_and_wait_protocol() == 0){
    return 0;
  }
  /* mest sannsynlig et ack som ligger å venter i rdp_multiplex og blir håntert med en gang rdp_write er blitt utført */
  return 1;
}


void rdp_request_reject(struct rdp_connection *new_connection, int connection_count, int metadata){
  int wc;
  struct rdp_packet *con_reject = new_packet(DENIED, 0, 0, connection_count, new_connection->client_id, metadata);
  /* metadata er definert i rdp.h */
  con_reject->metadata = htons(metadata);
  wc = send_packet(rdp_socket,
          (char*)con_reject,
          sizeof(struct rdp_packet),
          0,
          (struct sockaddr*)&new_connection->dest_addr,
          sizeof(struct sockaddr_in)
        );
  check_error(wc, "sendto");

  free(con_reject);
}


void rdp_request_accept(struct rdp_connection *new_connection, int connection_count){
  int wc;
  struct rdp_packet *con_accept = new_packet(ACCEPT, 0, 0, connection_count, new_connection->client_id, 0);

  wc = send_packet(rdp_socket,
          (char*)con_accept,
          sizeof(con_accept),
          0,
          (struct sockaddr*)&new_connection->dest_addr,
          sizeof(struct sockaddr_in)
        );
  check_error(wc, "sendto");
  free(con_accept);
}

/* i min tolkning av rdp, kan ikke samme klient ha to forskjellige rdp forbindelser til samme server
 * for hver forbindelsesforespørsel sjekker jeg derfor om klienten allerede er koblet til server
 * gjennom RDP */
int is_connected(int client_id){
  struct rdp_connection *temp = head->next;
  while (temp != tail){
    if ((temp->client_id) == client_id){
      return 1;
    }
    temp = temp->next;
  }
  return 0;
}

struct rdp_connection *rdp_accept(int connections){
  struct rdp_connection *new_connection = malloc(sizeof(struct rdp_connection));
  struct rdp_packet *con_rqst;
  struct rdp_connection_queue *queue_pointer = queue_head->next_in_line;

  if (connections == 0 || queue_pointer == NULL){
    // det betyr at det er første pakken
    int rc;
    con_rqst = malloc(sizeof(struct rdp_packet));
    rc = recvfrom(rdp_socket,
              con_rqst,
              sizeof(con_rqst),
              0,
              (struct sockaddr *)&client_addr,
              &addr_len);
    check_error(rc, "recvfrom");

    memcpy(&new_connection->dest_addr, &client_addr, addr_len);
  } else {

    con_rqst = queue_pointer->pckt;
    queue_head->next_in_line = queue_pointer->next_in_line;
    memcpy(&new_connection->dest_addr, &queue_pointer->dest_addr, addr_len);
    free(queue_pointer);
    queue_count--;
  }

  if (new_connection->dest_addr == NULL){
    perror("strdup");
    free(new_connection);
    rdp_free_connections();
    exit(EXIT_FAILURE);
  }
  /* setter verdier til rdp forbindelsen */
  new_connection->client_id = htons(con_rqst->senderid);
  new_connection->server_id = connections;
  new_connection->payload_number = 0;
  new_connection->pktseq = 0;

  if (con_rqst->flag != SYN){
    check_packet(con_rqst);
    free(new_connection);
    free(con_rqst);
    return NULL;
  }
  /* alle forbindlelser som ikke allerede er i datastrukturen til RDP blir godkjent
   * så er det opp til applikasjonen som bruker rdp om de vil fortsette å kommunisere med
   * klienten */
  else if (!is_connected(htons(con_rqst->senderid))){
    add_connection_to_rdp(head, new_connection, head->next);
    free(con_rqst);
    return new_connection;

  } else {
    // sender med metadata EXISTS som er definert i rdp.h
    rdp_request_reject(new_connection, connections, EXISTS);
    free(new_connection);
    free(con_rqst);
    return NULL;
  }

}

/* binder opp serveren til RDP */
void rdp_bind_port(unsigned short port){
  int rc;
  /* gjør klar den dynamiske allokerte datastrukturen og en kø for innkommende forbindelser */
  rdp_set_default_values();

  rdp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  check_error(rdp_socket, "socket");

  server_ip.s_addr = INADDR_ANY;

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = port;
  server_addr.sin_addr = server_ip;

  addr_len = sizeof(struct sockaddr_in);
  rc = bind(rdp_socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
  check_error(rc, "bind");

}
