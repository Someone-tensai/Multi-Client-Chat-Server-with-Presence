#ifndef REGISTRY_H
#define REGISTRY_H

#include <pthread.h>
#include <time.h>
#include "protocol.h"

// Define Constants
#define HISTORY_SIZE 10
#define MAX_MEMBERS  32
#define MAX_ROOMS 16

// Forward declaration
typedef struct client_t client_t;
typedef struct room_t room_t;
typedef struct message_t message_t;


// Struct to keep Message
typedef struct message_t {
    char sender[MAX_USERNAME_LEN];
    char text[MAX_TEXT_LEN];
    time_t timestamp;
} message_t;

// Room Defintion
typedef struct room_t {
    int room_id;
    char room_name[MAX_ROOM_NAME_LEN];
    client_t * members[MAX_MEMBERS];
    int member_count;
    client_t* admin_client;

    message_t history[HISTORY_SIZE];
    int history_count;
    int history_next;

} room_t;

// Client Definition
typedef struct client_t {
    int socket_fd;
    int client_id;
    char client_name[MAX_USERNAME_LEN];
    room_t* current_room;

}client_t;

// Mutex Lock to protect all room and member states from race conditions
extern pthread_mutex_t registry_lock;

// Room List and Count
extern room_t *room_list[MAX_ROOMS];
extern int room_count;

room_t* find_or_create_room(const char *room_name, client_t *creator_client);
int room_add_member(room_t *room, client_t *client);
int room_remove_member(room_t *room, client_t *client);
void room_broadcast(room_t *room, const char *msg, int exclude_fd);
client_t* registry_find_client(const char *username);


#endif