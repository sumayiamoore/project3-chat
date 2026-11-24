#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "list.h"

#define TRUE 1
#define FALSE 0
#define PORT 8888
#define BACKLOG 20
#define MAXBUFF 2096
#define DEFAULT_ROOM "Lobby"
#define SHUTDOWN_DELAY 3   /* seconds of warning before the server goes down */

/* shared state (defined in server.c) */
extern int chat_serv_sock_fd;
extern int numReaders;
extern pthread_mutex_t rw_lock;   /* held by a writer, or by the group of readers */
extern pthread_mutex_t mutex;     /* protects numReaders */
extern struct node *head;         /* all users */
extern struct room *rooms;        /* all rooms */
extern struct dm *dms;            /* all direct connections */
extern const char *server_MOTD;

/* reader-writer lock helpers (server.c) */
void reader_lock(void);
void reader_unlock(void);
void writer_lock(void);
void writer_unlock(void);

/* server.c */
int get_server_socket();
int start_server(int serv_socket, int backlog);
int accept_client(int serv_sock);
void sigintHandler(int sig_num);

/* server_client.c */
void *client_receive(void *ptr);

#endif