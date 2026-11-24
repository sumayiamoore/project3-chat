#include "server.h"

#define PROMPT "\nchat>"

static void sendText(int sock, const char *text) {
    send(sock, text, strlen(text), MSG_NOSIGNAL);
}

/* reply to the user who typed the command, then show the prompt again */
static void reply(int sock, const char *text) {
    sendText(sock, text);
    sendText(sock, PROMPT);
}

/* Trim leading and trailing whitespace (telnet sends "\r\n"). */
static char *trimwhitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int validName(const char *name) {
    return name != NULL && name[0] != '\0' && strlen(name) < NAME_LEN;
}

/* exit/logout: remove the user from every room, every DM and the user list */
static void logoutUser(int client) {
    writer_lock();
    removeFromAllRooms(rooms, client);
    removeAllDMs(&dms, client);
    removeUser(&head, client);
    writer_unlock();
}

/* ======================= Commands ======================= */

static void cmdLogin(int client, const char *name) {
    char buf[MAXBUFF];
    if (!validName(name)) { reply(client, "Usage: login <username> (max 29 chars)\n"); return; }

    writer_lock();
    struct node *taken = findUserByName(head, name);
    struct node *me = findUserBySocket(head, client);
    if (taken != NULL && taken->socket != client) {
        writer_unlock();
        snprintf(buf, sizeof(buf), "The username '%s' is already taken.\n", name);
        reply(client, buf);
        return;
    }
    char old[NAME_LEN] = "";
    if (me != NULL) {
        strcpy(old, me->username);
        strncpy(me->username, name, NAME_LEN - 1);
        me->username[NAME_LEN - 1] = '\0';
    }
    writer_unlock();
    /* rooms and DMs store sockets, so the new name shows up everywhere */

    snprintf(buf, sizeof(buf), "Logged in as %s (was %s).\n", name, old);
    reply(client, buf);
}

static void cmdCreate(int client, const char *name) {
    char buf[MAXBUFF];
    if (!validName(name)) { reply(client, "Usage: create <room> (max 29 chars)\n"); return; }

    writer_lock();
    if (findRoom(rooms, name) != NULL) {
        writer_unlock();
        snprintf(buf, sizeof(buf), "Room '%s' already exists.\n", name);
        reply(client, buf);
        return;
    }
    struct room *r = createRoom(&rooms, name);
    writer_unlock();

    if (r == NULL) snprintf(buf, sizeof(buf), "Could not create room '%s'.\n", name);
    else snprintf(buf, sizeof(buf), "Room '%s' created. Type 'join %s' to enter it.\n", name, name);
    reply(client, buf);
}

static void cmdJoin(int client, const char *name) {
    char buf[MAXBUFF];
    if (!validName(name)) { reply(client, "Usage: join <room>\n"); return; }

    writer_lock();
    struct room *r = findRoom(rooms, name);
    int rc = (r == NULL) ? -2 : roomAddMember(r, client);
    writer_unlock();

    if (rc == -2)     snprintf(buf, sizeof(buf), "Room '%s' does not exist.\n", name);
    else if (rc == 0) snprintf(buf, sizeof(buf), "You are already in '%s'.\n", name);
    else if (rc < 0)  snprintf(buf, sizeof(buf), "Could not join '%s'.\n", name);
    else              snprintf(buf, sizeof(buf), "Joined room '%s'.\n", name);
    reply(client, buf);
}

static void cmdLeave(int client, const char *name) {
    char buf[MAXBUFF];
    if (!validName(name)) { reply(client, "Usage: leave <room>\n"); return; }

    writer_lock();
    struct room *r = findRoom(rooms, name);
    int rc = (r == NULL) ? -1 : roomRemoveMember(r, client);
    writer_unlock();

    if (rc == -1)     snprintf(buf, sizeof(buf), "Room '%s' does not exist.\n", name);
    else if (rc == 0) snprintf(buf, sizeof(buf), "You are not in '%s'.\n", name);
    else              snprintf(buf, sizeof(buf), "Left room '%s'.\n", name);
    reply(client, buf);
}

static void cmdConnect(int client, const char *name) {
    char buf[MAXBUFF];
    if (!validName(name)) { reply(client, "Usage: connect <user>\n"); return; }

    writer_lock();
    struct node *me = findUserBySocket(head, client);
    struct node *other = findUserByName(head, name);
    int rc;
    if (other == NULL) rc = -2;
    else if (other->socket == client) rc = -3;
    else rc = addDM(&dms, client, other->socket);

    if (rc == 1 && me != NULL) {   /* let the other user know */
        snprintf(buf, sizeof(buf), "\n%s is now directly connected to you.%s", me->username, PROMPT);
        sendText(other->socket, buf);
    }
    writer_unlock();

    if (rc == -2)      snprintf(buf, sizeof(buf), "User '%s' not found.\n", name);
    else if (rc == -3) snprintf(buf, sizeof(buf), "You can't connect to yourself.\n");
    else if (rc == 0)  snprintf(buf, sizeof(buf), "You are already connected to %s.\n", name);
    else if (rc < 0)   snprintf(buf, sizeof(buf), "Could not connect to %s.\n", name);
    else               snprintf(buf, sizeof(buf), "Connected to %s.\n", name);
    reply(client, buf);
}

static void cmdDisconnect(int client, const char *name) {
    char buf[MAXBUFF];
    if (!validName(name)) { reply(client, "Usage: disconnect <user>\n"); return; }

    writer_lock();
    struct node *me = findUserBySocket(head, client);
    struct node *other = findUserByName(head, name);
    int rc = (other == NULL) ? -1 : removeDM(&dms, client, other->socket);

    if (rc == 1 && me != NULL) {
        snprintf(buf, sizeof(buf), "\n%s disconnected from you.%s", me->username, PROMPT);
        sendText(other->socket, buf);
    }
    writer_unlock();

    if (rc == -1)     snprintf(buf, sizeof(buf), "User '%s' not found.\n", name);
    else if (rc == 0) snprintf(buf, sizeof(buf), "You are not connected to %s.\n", name);
    else              snprintf(buf, sizeof(buf), "Disconnected from %s.\n", name);
    reply(client, buf);
}

/* read-only: list every user on the server, marking you and your DMs */
static void cmdUsers(int client) {
    char buf[MAXBUFF];
    size_t len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "Users:\n");

    reader_lock();
    for (struct node *u = head; u != NULL && len < sizeof(buf) - 64; u = u->next) {
        const char *tag = "";
        if (u->socket == client) tag = " (you)";
        else if (isConnected(dms, client, u->socket)) tag = " (DM)";
        len += snprintf(buf + len, sizeof(buf) - len, "  %s%s\n", u->username, tag);
    }
    reader_unlock();

    reply(client, buf);
}

/* read-only: list every room, its member count, and whether you're in it */
static void cmdRooms(int client) {
    char buf[MAXBUFF];
    size_t len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "Rooms:\n");

    reader_lock();
    for (struct room *r = rooms; r != NULL && len < sizeof(buf) - 64; r = r->next) {
        len += snprintf(buf + len, sizeof(buf) - len, "  %s (%d user%s)%s\n",
                        r->name, roomCount(r), roomCount(r) == 1 ? "" : "s",
                        roomHasMember(r, client) ? " *joined*" : "");
    }
    reader_unlock();

    reply(client, buf);
}

static void cmdHelp(int client) {
    reply(client,
          "Commands:\n"
          "  login <username>     change your name\n"
          "  create <room>        create a room\n"
          "  join <room>          join a room\n"
          "  leave <room>         leave a room\n"
          "  users                list all users\n"
          "  rooms                list all rooms\n"
          "  connect <user>       direct message a user\n"
          "  disconnect <user>    stop direct messages with a user\n"
          "  exit | logout        leave the chat\n"
          "Anything else is sent to everyone in your rooms and DMs.\n");
}

/* Selective send: a message goes to every user who shares at least one
 * room with the sender OR is directly connected to the sender.
 * Each recipient gets it once, even if they match more than one way. */
static void sendMessage(int client, const char *text) {
    char buf[MAXBUFF + NAME_LEN + 16];

    reader_lock();
    struct node *me = findUserBySocket(head, client);
    const char *myName = (me != NULL) ? me->username : "unknown";
    snprintf(buf, sizeof(buf), "\n::%s> %s%s", myName, text, PROMPT);

    for (struct node *u = head; u != NULL; u = u->next) {
        if (u->socket == client) continue;
        if (shareRoom(rooms, client, u->socket) || isConnected(dms, client, u->socket))
            sendText(u->socket, buf);
    }
    reader_unlock();

    sendText(client, "chat>");
}

/* Handle one line of input. Returns 0 if the user wants to exit. */
static int handleLine(int client, char *raw) {
    char *line = trimwhitespace(raw);
    if (*line == '\0') { sendText(client, "chat>"); return 1; }

    char copy[MAXBUFF];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *cmd = strtok(copy, " \t");
    char *arg = strtok(NULL, " \t");

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "logout") == 0) return 0;
    else if (strcmp(cmd, "login") == 0)      cmdLogin(client, arg);
    else if (strcmp(cmd, "create") == 0)     cmdCreate(client, arg);
    else if (strcmp(cmd, "join") == 0)       cmdJoin(client, arg);
    else if (strcmp(cmd, "leave") == 0)      cmdLeave(client, arg);
    else if (strcmp(cmd, "users") == 0)      cmdUsers(client);
    else if (strcmp(cmd, "rooms") == 0)      cmdRooms(client);
    else if (strcmp(cmd, "connect") == 0)    cmdConnect(client, arg);
    else if (strcmp(cmd, "disconnect") == 0) cmdDisconnect(client, arg);
    else if (strcmp(cmd, "help") == 0)       cmdHelp(client);
    else sendMessage(client, line);          /* not a command: it's chat */
    return 1;
}

/* One of these threads runs for each connected client. */
void *client_receive(void *ptr) {
    int client = *(int *)ptr;
    free(ptr);

    /* Initial login: guest<socket>, placed in the Lobby */
    char guest[NAME_LEN];
    snprintf(guest, sizeof(guest), "guest%d", client);

    writer_lock();
    insertUser(&head, client, guest);
    struct room *lobby = findRoom(rooms, DEFAULT_ROOM);
    if (lobby != NULL) roomAddMember(lobby, client);
    writer_unlock();

    char welcome[256];
    snprintf(welcome, sizeof(welcome),
             "Thanks for connecting to the BisonChat Server.\n"
             "You are %s and you are in the %s. Type 'help' for commands.\n\nchat>",
             guest, DEFAULT_ROOM);
    sendText(client, welcome);
    printf("New client: %s\n", guest);

    /* Read input and split it into lines; one read() may hold part
     * of a line or several lines. */
    char buffer[MAXBUFF];
    char line[MAXBUFF];
    size_t len = 0;
    ssize_t received;
    int running = 1;

    while (running && (received = read(client, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < received && running; i++) {
            unsigned char c = buffer[i];
            if (c == '\n') {
                line[len] = '\0';
                running = handleLine(client, line);
                len = 0;
            } else if (c == '\t' || (c >= 32 && c < 127)) {   /* skip \r and telnet control bytes */
                if (len < sizeof(line) - 1) line[len++] = (char)c;
            }
        }
    }

    /* exit/logout typed, or the client dropped the connection */
    if (running == 0) sendText(client, "Goodbye!\n");
    logoutUser(client);
    printf("Client %s (socket %d) left.\n", guest, client);
    close(client);
    return NULL;
}