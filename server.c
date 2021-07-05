#include "rdp.h"
#include "new_fsp_server.h"

unsigned int PORT;
int N;
char *filename;

void validate_args(int argc, char *argv[]){

  if (argc < 4){
    printf("usage: %s <my port> <filename> <num of files> <loss probability>\n", argv[0]);
    exit(EXIT_SUCCESS);
  }

  PORT = htons(atoi(argv[1]));
  filename = argv[2];
  N = atoi(argv[3]);
  set_loss_probability((float)strtod(argv[4],NULL));


}


int main( int argc, char *argv[]){
  validate_args(argc, argv);
  new_fsp_run_program(PORT, N, filename);

  return 0;
}
