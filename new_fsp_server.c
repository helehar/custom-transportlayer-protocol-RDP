#include "new_fsp_server.h"
#include "rdp.h"

/* dynamisk allokert lenkeliste fordi den skal gjøre plass
 * til en fil av hvilken som helst størrelse - og lagre den
 * i minnet */
struct server_storage *storage_head, *storage_tail;

/* statisk allokert lenkeliste over klienter som skal motta fil */
struct file_reciever **newFSP_connections;
/* N er antall filer server skal overføre, og connection_count
 * er oversikt på klienter som er koblet til server gjennom RDP */
int N, connection_count = 0;

/* brueks for at applikasjonen skal holde styr på hvilken pakke som
 * skal til hvilken klient  */
struct server_storage * newfsp_payload_by_id(int payload_id){
  struct server_storage *temp = storage_head->next;
  while (temp != storage_tail){
    if ((temp->id) == payload_id){
      return temp;
    }
    temp = temp->next;
  }
  return NULL;
}

/* kode hentet fra cebra */
void check_result(int res, FILE *f){
  if (res == 0 && ferror(f)) {
    fprintf(stderr, "error performing read/write\n");
    fclose(f);
    exit(EXIT_FAILURE);
  }
}

/* kode hentet fra cebra */
void free_storage() {
  struct server_storage *next, *temp = storage_head -> next;
  while (temp != storage_tail){
    next = temp ->next;
    free(temp);
    temp = next;
  }
  free(storage_head);
  free(storage_tail);
}

void free_remaining_clients(){
  if (connection_count > 0){
    struct file_reciever *temp;
    for (int i = 0; i < connection_count; i++){
      temp = newFSP_connections[i];
      free(temp);
    }
  }
  free(newFSP_connections);
}

/* brukt til testing */
void print_memory() {
  struct server_storage *temp = storage_tail -> prev;
  while (temp != storage_head){
    temp = temp->prev;
  }
}

/* kode hentet fra cebra */
void add_storage(struct server_storage *left, struct server_storage *middle, struct server_storage *right){
  left->next = middle;
  right->prev = middle;
  middle->next = right;
  middle->prev = left;
}

/* kode hentet fra cebra */
void remove_storage(struct server_storage *storage){
  storage->prev->next = storage->next;
  storage->next->prev = storage->prev;
}

/* kode hentet fra cebra */
void newfsp_set_default_values(){
  storage_head = malloc(sizeof(struct server_storage));
  storage_tail = malloc(sizeof(struct server_storage));
  storage_head -> next = storage_tail;
  storage_tail -> prev = storage_head;
}

/* legger til filpakker i minnet */
void append_storage_chunk(char payload[], int id, int bytes){
  struct server_storage *new_storage = malloc(sizeof (struct server_storage) + bytes);
  memcpy(new_storage->payload, payload, bytes);

  if (new_storage->payload == NULL){
    perror("strdup");
    rdp_free_connections();
    exit(EXIT_FAILURE);
  }
  new_storage->id = id;
  new_storage->size = bytes;
  add_storage(storage_head, new_storage, storage_head->next);

}

void add_file_to_memory(char *filename){
  FILE *file;
  size_t rc;
  /* nyttelasten som sendes fra server til klient kan være maks
   * 1000 bytes */
  char tempbuf[1000];
  int num_of_blocks = 0, remainder = 0;
  unsigned int index = 0;
  newfsp_set_default_values();

  file = fopen(filename, "rb");
  if (file == NULL){
    perror("fopen");
    rdp_free_connections();
    exit( EXIT_FAILURE);
  }

  fseek(file, 0, SEEK_END);
  long bytes = ftell(file);
  rewind(file);

  /* under følger det kode som legger til fil i minnet */
  if (bytes > 1000){
    remainder = bytes % 1000;
    if (remainder != 0){
      bytes = bytes - remainder; // tar vekk resten
      num_of_blocks = (bytes / 1000);
    } else {
      num_of_blocks = (bytes / 1000);
    }
    for (int i = 0; i < num_of_blocks; i++){
      rc = fread(tempbuf, sizeof(unsigned char), 1000, file);
      check_result(rc, file);

      if (rc) append_storage_chunk(tempbuf, index, rc);
      index++;
    }
    if (remainder != 0){
      rc = fread(tempbuf, sizeof(unsigned char), remainder, file);
      check_result(rc, file);

      append_storage_chunk(tempbuf, index, remainder);
      index++;
    }

  } else {
    rc = fread(tempbuf, sizeof(unsigned char), bytes, file);
    check_result(rc, file);

    if (rc) append_storage_chunk(tempbuf, index, bytes);
    index++;
  }
  /* legger til en tom pakke på slutten
   * indikerer slutten på fila */
  tempbuf[0] = 0;
  append_storage_chunk(tempbuf, index, 0);

  fclose(file);

}

struct file_reciever *create_newFSP_connection(int client_id, int server_id){
  struct file_reciever *fr = malloc(sizeof(struct file_reciever));
  if (fr == NULL){
    fprintf(stderr, "malloc failed. Possibly out of memory\n");
    free(fr);
    free_storage();
    free_remaining_clients();
    exit(EXIT_FAILURE);
  }
  fr -> client_id = client_id;
  fr ->server_id = server_id;
  return fr;

}

void delete_newFSP_connection(struct file_reciever *fr){
  for (int i = 0; i < connection_count; i++){
    if (newFSP_connections[i]->client_id == fr->client_id){
      int del;
      for (del = i; del < connection_count-1; ++del){
        newFSP_connections[del] = newFSP_connections[del+1];
      }
    }
  }
}

/* Hovedløkka og selve newFSP prosessen */
void new_fsp_run_program(unsigned short PORT, int n, char *filename){
  /* ting som må håndteres før prosessen kan starte:
   * serveren må koble seg opp til RDP, og legge inn filen som skal overføres
   * i minnet */
  rdp_bind_port(PORT);
  add_file_to_memory(filename);

  N = n;

  /* fyller den statiske listen med null, og gjør klar til at nye file_recivers
   * kan legges inn */
  newFSP_connections = malloc(sizeof(struct file_reciever) * N);
  for (int i = 0; i < N; i++){
    newFSP_connections[i] = NULL;
  }

  /* serveren terminerer når denne er lik N */
  int newFSP_file_complete = 0;

  /* hver RDP kobling krever en server_id og en client_id
   * jeg har valgt å løse det slik at serveren bare øker sin id for hver gang den får en
   * ny kobling. */
  int server_id = 0;

  /* newfsp kan ikke sende fil før den har fått en kobling, derfor kan ikke hovedløkka starte før
   * første kobling er på plass */
  struct rdp_connection *first_connection =  rdp_accept(server_id);
  if (first_connection != NULL){

    struct file_reciever *fr = create_newFSP_connection(first_connection->client_id, first_connection->server_id);
    newFSP_connections[connection_count] = fr;
    rdp_request_accept(first_connection, connection_count);
    printf("CONNECTED <%d> <%d>\n", first_connection->server_id, first_connection->client_id);
    connection_count++;
    server_id++;
  }

  while (1){
    int i = 0, rc_1;

    for (i = 0; i < connection_count; i++){
      rc_1 = rdp_multiplex();
      if (rc_1 == 3){
        /* server har fått en connection request */
        struct rdp_connection *new_connection = rdp_accept(server_id);
        if (new_connection != NULL){
          /* men server har bare N filer å levere ut, og kan derfor ikke ta inn flere enn N */
          if (server_id < N){
            struct file_reciever *fr = create_newFSP_connection(new_connection->client_id, new_connection->server_id);
            newFSP_connections[connection_count] = fr;

            rdp_request_accept(new_connection, server_id);
            printf("CONNECTED <%d> <%d>\n", new_connection->server_id, new_connection->client_id);
            connection_count++;
            server_id++;
          } else {
            /* rdp har jo ikke oversikt over tallet N, så det er det new fsp som må holde styr på */
            printf("NOT CONNECTED <%d> <%d>\n", new_connection->server_id, new_connection->client_id);
            rdp_request_reject(new_connection, server_id, FULL);
            rdp_remove_connection(new_connection);
          }
        }
      }
      /* henter neste forbindelse i oversikten */
      struct file_reciever *temp_connection = newFSP_connections[i];
      /* newfsp sender en forespørsel ned til RDP transportlaget om klienten ligger i lista */
      struct rdp_connection *client = rdp_connection_by_id(temp_connection->client_id);

      if (client != NULL){
        /* henter neste pakke som skal sendes til klienten */
        struct server_storage *payload_packet = newfsp_payload_by_id(client->payload_number);
        if (payload_packet != NULL){
          rdp_write(client, payload_packet->payload, payload_packet->size);
        }
      } else if(client == NULL){
          /* dersom client er null betyr det at rdp protokollen har fått en FIN, og fjernet klienten fra
           * sin dynamiske allokerte datastruktur */
          printf("DISCONNECTED <%d> <%d>\n", temp_connection->server_id, temp_connection->client_id);
          newFSP_file_complete++;

          delete_newFSP_connection(temp_connection);
          connection_count--;
          free(temp_connection);


        }
      }
    /* dersom ingen flere klienter å sende fil til, men server fortsatt har flere filer å sende */
    if (connection_count == 0 && newFSP_file_complete < N){
      rc_1 = rdp_multiplex();
      while (rc_1== 0){
        rc_1 = rdp_multiplex();
      }

      /* gjentar fra for løkka */
      if (rc_1 == 3){
        struct rdp_connection *new_connection = rdp_accept(server_id);
        if (new_connection != NULL){
          if (server_id < N){
            struct file_reciever *fr = create_newFSP_connection(new_connection->client_id, new_connection->server_id);
            newFSP_connections[connection_count] = fr;

            rdp_request_accept(new_connection, server_id);
            printf("CONNECTED <%d> <%d>\n", new_connection->server_id, new_connection->client_id);
            connection_count++;
            server_id++;
          } else {
            printf("NOT CONNECTED <%d> <%d>\n", new_connection->server_id, new_connection->client_id);
            rdp_request_reject(new_connection, server_id, FULL);
            rdp_remove_connection(new_connection);
          }
        }
      }
    /* newfps server har overført alle filer og er ferdig med jobben sin */
    }else if (newFSP_file_complete == N){
      free_remaining_clients();
      rdp_free_connections();
      free_storage();
      exit(EXIT_SUCCESS);
    }

  }

}
