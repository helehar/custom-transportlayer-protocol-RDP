#include "rdp.h"
#include <time.h>
/* klienten har jeg ikke så mye kommentarer til
 * den kobler seg opp til en newfsp server for å motta pakker  */

unsigned int PORT;
static int client_id;

void recieve_file(int client_id, struct rdp_connection *connection){
  char filename[100];
  sprintf( filename, "kernel-file-%d.dat", client_id);

  FILE *kernelFile = fopen(filename, "wb");
  if (kernelFile == NULL){
    perror("fopen");
    free(connection);
    exit(EXIT_FAILURE);
  }

  int ptr_size = 1;

  while (1){
    if (ptr_size < 1){
      break;
    }
    struct rdp_app_layer_packet *pointer = rdp_read(connection, 1000);
    if (pointer){
      fwrite(pointer->payload, sizeof(char), pointer->size, kernelFile);
      ptr_size = pointer->size;
    }
    free(pointer);
  }

  rdp_close(connection);
  fclose(kernelFile);
  printf("%s\n", filename);
  free(connection);
  exit(EXIT_SUCCESS);
}

void connect_to_server(char *ip){
  srand(time(0));
  client_id = rand() / PORT;

  struct rdp_connection *connection = rdp_connect(ip, client_id, PORT);
  if (connection != NULL){
    printf("CONNECTED <%d> <%d>\n", connection->server_id, connection->client_id);
    recieve_file(client_id, connection);
  }else {
    /* sover et sekund hvis den ikke hører noe */
    sleep(1);
    printf("ERROR CONNECTING TO SERVER\n");
    exit (EXIT_SUCCESS);
  }
}

void validate_args(int argc, char *argv[]){
  if (argc < 4){
    printf("usage: %s <server port> <server ip> <loss probability>\n", argv[0]);
    exit(EXIT_SUCCESS);
  }

  PORT = htons(atoi(argv[1]));
  set_loss_probability((float)strtod(argv[3],NULL));

}


int main(int argc, char *argv[]){
  validate_args(argc, argv);
  connect_to_server(argv[2]);

  return 0;
}
