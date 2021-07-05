#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

/* skal holde filen i minnet */
struct server_storage {
  char payload[1000];
  int id;
  int size;
  struct server_storage *next;
  struct server_storage *prev;
};

/* en statisk liste med N "file_recivers".
 * siden newfsp er en filtjeneste applikasjon
 * har jeg tatt oppgaven på ordet og kalt hver
 * tilkoblede klient for en "file_reciever" */
struct file_reciever {
  int client_id;
  int server_id;
};

void new_fsp_run_program(unsigned short PORT, int n, char *filename);



#endif
