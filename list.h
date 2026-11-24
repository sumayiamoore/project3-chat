#ifndef LIST_H
#define LIST_H

#define NAME_LEN 30

/* ---------- Users ----------
 * One node per connected client: their name and socket descriptor.
 * The socket is the unique key for a user (it never changes),
 * while the username can change with "login".
 */
struct node {
    char username[NAME_LEN];
    int socket;
    struct node *next;
};

/* ---------- Rooms ----------
 * Each room keeps a list of member sockets. Storing sockets
 * (not names) means a "login" rename shows up in every room
 * and DM automatically.
 */
struct member {
    int socket;
    struct member *next;
};

struct room {
    char name[NAME_LEN];
    struct member *members;
    struct room *next;
};

/* ---------- Direct connections (DMs) ----------
 * One node per pair of connected users. A DM goes both ways.
 */
struct dm {
    int user1;
    int user2;
    struct dm *next;
};

/* users */
struct node *insertUser(struct node **head, int socket, const char *username);
struct node *findUserByName(struct node *head, const char *username);
struct node *findUserBySocket(struct node *head, int socket);
int removeUser(struct node **head, int socket);
void freeUsers(struct node **head);

/* rooms */
struct room *createRoom(struct room **head, const char *name);
struct room *findRoom(struct room *head, const char *name);
int roomHasMember(struct room *r, int socket);
int roomAddMember(struct room *r, int socket);
int roomRemoveMember(struct room *r, int socket);
int roomCount(struct room *r);
void removeFromAllRooms(struct room *head, int socket);
int shareRoom(struct room *head, int a, int b);
void freeRooms(struct room **head);

/* direct connections */
int isConnected(struct dm *head, int a, int b);
int addDM(struct dm **head, int a, int b);
int removeDM(struct dm **head, int a, int b);
void removeAllDMs(struct dm **head, int socket);
void freeDMs(struct dm **head);

#endif