#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"

static void copyName(char *dst, const char *src) {
    strncpy(dst, src, NAME_LEN - 1);
    dst[NAME_LEN - 1] = '\0';
}

/* ======================= Users ======================= */

struct node *insertUser(struct node **head, int socket, const char *username) {
    struct node *n = malloc(sizeof(struct node));
    if (n == NULL) return NULL;
    n->socket = socket;
    copyName(n->username, username);
    n->next = *head;
    *head = n;
    return n;
}

struct node *findUserByName(struct node *head, const char *username) {
    for (struct node *cur = head; cur != NULL; cur = cur->next)
        if (strcmp(cur->username, username) == 0) return cur;
    return NULL;
}

struct node *findUserBySocket(struct node *head, int socket) {
    for (struct node *cur = head; cur != NULL; cur = cur->next)
        if (cur->socket == socket) return cur;
    return NULL;
}

int removeUser(struct node **head, int socket) {
    struct node **pp = head;
    while (*pp != NULL) {
        if ((*pp)->socket == socket) {
            struct node *dead = *pp;
            *pp = dead->next;
            free(dead);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

void freeUsers(struct node **head) {
    struct node *cur = *head;
    while (cur != NULL) {
        struct node *next = cur->next;
        free(cur);
        cur = next;
    }
    *head = NULL;
}

/* ======================= Rooms ======================= */

struct room *createRoom(struct room **head, const char *name) {
    struct room *r = malloc(sizeof(struct room));
    if (r == NULL) return NULL;
    copyName(r->name, name);
    r->members = NULL;
    r->next = NULL;
    struct room **pp = head;
    while (*pp != NULL) pp = &(*pp)->next;
    *pp = r;
    return r;
}

struct room *findRoom(struct room *head, const char *name) {
    for (struct room *cur = head; cur != NULL; cur = cur->next)
        if (strcmp(cur->name, name) == 0) return cur;
    return NULL;
}

int roomHasMember(struct room *r, int socket) {
    for (struct member *m = r->members; m != NULL; m = m->next)
        if (m->socket == socket) return 1;
    return 0;
}

int roomAddMember(struct room *r, int socket) {
    if (roomHasMember(r, socket)) return 0;
    struct member *m = malloc(sizeof(struct member));
    if (m == NULL) return -1;
    m->socket = socket;
    m->next = r->members;
    r->members = m;
    return 1;
}

int roomRemoveMember(struct room *r, int socket) {
    struct member **pp = &r->members;
    while (*pp != NULL) {
        if ((*pp)->socket == socket) {
            struct member *dead = *pp;
            *pp = dead->next;
            free(dead);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

int roomCount(struct room *r) {
    int count = 0;
    for (struct member *m = r->members; m != NULL; m = m->next) count++;
    return count;
}

void removeFromAllRooms(struct room *head, int socket) {
    for (struct room *r = head; r != NULL; r = r->next)
        roomRemoveMember(r, socket);
}

int shareRoom(struct room *head, int a, int b) {
    for (struct room *r = head; r != NULL; r = r->next)
        if (roomHasMember(r, a) && roomHasMember(r, b)) return 1;
    return 0;
}

void freeRooms(struct room **head) {
    struct room *r = *head;
    while (r != NULL) {
        struct member *m = r->members;
        while (m != NULL) {
            struct member *nextm = m->next;
            free(m);
            m = nextm;
        }
        struct room *nextr = r->next;
        free(r);
        r = nextr;
    }
    *head = NULL;
}

/* ================= Direct connections ================= */

int isConnected(struct dm *head, int a, int b) {
    for (struct dm *d = head; d != NULL; d = d->next)
        if ((d->user1 == a && d->user2 == b) || (d->user1 == b && d->user2 == a))
            return 1;
    return 0;
}

int addDM(struct dm **head, int a, int b) {
    if (isConnected(*head, a, b)) return 0;
    struct dm *d = malloc(sizeof(struct dm));
    if (d == NULL) return -1;
    d->user1 = a;
    d->user2 = b;
    d->next = *head;
    *head = d;
    return 1;
}

int removeDM(struct dm **head, int a, int b) {
    struct dm **pp = head;
    while (*pp != NULL) {
        struct dm *d = *pp;
        if ((d->user1 == a && d->user2 == b) || (d->user1 == b && d->user2 == a)) {
            *pp = d->next;
            free(d);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

void removeAllDMs(struct dm **head, int socket) {
    struct dm **pp = head;
    while (*pp != NULL) {
        struct dm *d = *pp;
        if (d->user1 == socket || d->user2 == socket) {
            *pp = d->next;
            free(d);
        } else {
            pp = &(*pp)->next;
        }
    }
}

void freeDMs(struct dm **head) {
    struct dm *d = *head;
    while (d != NULL) {
        struct dm *next = d->next;
        free(d);
        d = next;
    }
    *head = NULL;
}
