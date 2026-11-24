#include "server.h"

int chat_serv_sock_fd; //server socket

/////////////////////////////////////////////
// USE THESE LOCKS AND COUNTER TO SYNCHRONIZE

int numReaders = 0; // keep count of the number of readers

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  // mutex lock
pthread_mutex_t rw_lock = PTHREAD_MUTEX_INITIALIZER;  // read/write lock

/////////////////////////////////////////////


char const *server_MOTD = "Thanks for connecting to the BisonChat Server.\n\nchat>";

struct node *head = NULL;   // all users
struct room *rooms = NULL;  // all rooms
struct dm *dms = NULL;      // all direct connections (DMs)


/////////////////////////////////////////////
// Reader-writer lock helpers (textbook readers-writers):
// the first reader locks rw_lock, the last reader unlocks it,
// and a writer holds rw_lock by itself.

void reader_lock(void) {
   pthread_mutex_lock(&mutex);
   numReaders++;
   if(numReaders == 1) pthread_mutex_lock(&rw_lock);   // first reader blocks writers
   pthread_mutex_unlock(&mutex);
}

void reader_unlock(void) {
   pthread_mutex_lock(&mutex);
   numReaders--;
   if(numReaders == 0) pthread_mutex_unlock(&rw_lock); // last reader lets writers in
   pthread_mutex_unlock(&mutex);
}

void writer_lock(void)   { pthread_mutex_lock(&rw_lock); }
void writer_unlock(void) { pthread_mutex_unlock(&rw_lock); }
/////////////////////////////////////////////


int main(int argc, char **argv) {
   (void)argc; (void)argv;

   signal(SIGINT, sigintHandler);
   signal(SIGPIPE, SIG_IGN);   // writing to a closed client should not kill the server

   //////////////////////////////////////////////////////
   // create the default room for all clients to join when
   // initially connecting
   writer_lock();
   if(createRoom(&rooms, DEFAULT_ROOM) == NULL) {
      printf("could not create %s\n", DEFAULT_ROOM);
      exit(1);
   }
   writer_unlock();
   //////////////////////////////////////////////////////

   // Open server socket
   chat_serv_sock_fd = get_server_socket();

   // step 3: get ready to accept connections
   if(start_server(chat_serv_sock_fd, BACKLOG) == -1) {
      printf("start server error\n");
      exit(1);
   }

   printf("Server Launched! Listening on PORT: %d\n", PORT);

   // client threads block SIGINT so Ctrl-C is always handled by this
   // main thread (which never holds a lock while waiting in accept)
   sigset_t block, old;
   sigemptyset(&block);
   sigaddset(&block, SIGINT);

   //Main execution loop
   while(1) {
      //Accept a connection, start a thread
      int new_client = accept_client(chat_serv_sock_fd);
      if(new_client != -1) {
         // give each thread its own copy of the socket number;
         // passing &new_client lets two threads share one value
         int *client_ptr = malloc(sizeof(int));
         if(client_ptr == NULL) { close(new_client); continue; }
         *client_ptr = new_client;

         pthread_t new_client_thread;
         pthread_sigmask(SIG_BLOCK, &block, &old);
         int rc = pthread_create(&new_client_thread, NULL, client_receive, (void *)client_ptr);
         pthread_sigmask(SIG_SETMASK, &old, NULL);

         if(rc != 0) {
            printf("thread create error\n");
            free(client_ptr);
            close(new_client);
         } else {
            pthread_detach(new_client_thread);   // thread cleans itself up when it ends
         }
      }
   }

   close(chat_serv_sock_fd);
}


int get_server_socket() {
    int opt = TRUE;
    int master_socket;
    struct sockaddr_in address;

    //create a master socket
    if( (master_socket = socket(AF_INET , SOCK_STREAM , 0)) == -1)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    //set master socket to allow multiple connections ,
    //this is just a good habit, it will work without this
    if( setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
          sizeof(opt)) < 0 )
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    //type of socket created
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( PORT );

    //bind the socket to localhost port 8888
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address))<0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

   return master_socket;
}


int start_server(int serv_socket, int backlog) {
   int status = 0;
   if ((status = listen(serv_socket, backlog)) == -1) {
      printf("socket listen error\n");
   }
   return status;
}


int accept_client(int serv_sock) {
   int reply_sock_fd = -1;
   socklen_t sin_size = sizeof(struct sockaddr_storage);
   struct sockaddr_storage client_addr;
   //char client_printable_addr[INET6_ADDRSTRLEN];

   // accept a connection request from a client
   // the returned file descriptor from accept will be used
   // to communicate with this client.
   if ((reply_sock_fd = accept(serv_sock,(struct sockaddr *)&client_addr, &sin_size)) == -1) {
      printf("socket accept error\n");
   }
   return reply_sock_fd;
}


// send a message to every connected user
static void broadcastAll(const char *msg) {
   for(struct node *u = head; u != NULL; u = u->next)
      send(u->socket, msg, strlen(msg), MSG_NOSIGNAL);
}


/* Handle SIGINT (CTRL+C) */
void sigintHandler(int sig_num) {
   (void)sig_num;
   char msg[128];

   printf("Error:Forced Exit.\n");

   // stop accepting new clients
   close(chat_serv_sock_fd);

   // stretch goal: warn everyone the server is going down
   writer_lock();
   snprintf(msg, sizeof(msg), "\n*** Server is going down in %d seconds. ***\nchat>", SHUTDOWN_DELAY);
   broadcastAll(msg);
   writer_unlock();
   printf("Shutting down in %d seconds...\n", SHUTDOWN_DELAY);
   sleep(SHUTDOWN_DELAY);

   //////////////////////////////////////////////////////////
   //Closing client sockets and freeing memory from user list
   //
   // keep the writer lock until exit() so no client thread
   // can touch the lists while they are being freed
   writer_lock();

   printf("--------CLOSING ACTIVE USERS--------\n");

   broadcastAll("\n*** Server shut down. Goodbye! ***\n");
   for(struct node *u = head; u != NULL; u = u->next) {
      printf("closing %s (socket %d)\n", u->username, u->socket);
      shutdown(u->socket, SHUT_RDWR);
      close(u->socket);
   }

   freeDMs(&dms);
   freeRooms(&rooms);
   freeUsers(&head);
   printf("all rooms, DMs and users freed\n");
   //////////////////////////////////////////////////////////

   fflush(stdout);
   exit(0);
}