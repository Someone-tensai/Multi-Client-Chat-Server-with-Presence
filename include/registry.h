#ifndef REGISTRY_H
#define REGISTRY_H

#include <pthread.h>
#include <time.h>
#include "protocol.h"

// Define Constants
#define HISTORY_SIZE 10
#define MAX_CLIENTS 64
#define MAX_MEMBERS 16
#define MAX_ROOMS 16

// Forward declaration
typedef struct client_t client_t;
typedef struct room_t room_t;
typedef struct message_t message_t;

// Error Codes for Room 
typedef enum {
    ROOM_OK = 0,
    ROOM_ERR_INVALID_NAME,
    ROOM_ERR_ALREADY_EXISTS,
    ROOM_ERR_ALREADY_IN_A_ROOM,
    ROOM_ERR_MAX_ROOMS,
    ROOM_ERR_ALLOC_FAILED,
    ROOM_ERR_MEMBER_NOT_IN_ROOM,
    ROOM_ERR_NOT_FOUND,
    ROOM_ERR_NULL,
    ROOM_ERR_MAX_MEMBERS,
    ROOM_ERR_INVALID_CLIENT,
    ROOM_ERR_CLIENT_NOT_FOUND,
} room_err_t;

// Error Codes for Client
typedef enum {
    CLIENT_OK = 0,
    CLIENT_ERR_INVALID_NAME,
    CLIENT_ERR_ALREADY_EXISTS,
    CLIENT_ERR_MAX_CLIENTS,
    CLIENT_ERR_ALLOC_FAILED,
    CLIENT_ERR_NOT_FOUND,
    CLIENT_ERR_NULL,

} client_err_t;


// Struct to keep Message
typedef struct message_t {
    char sender[MAX_USERNAME_LEN];
    char text[MAX_TEXT_LEN];
    time_t timestamp;
} message_t;

// Room Defintion
typedef struct room_t {
    char room_name[MAX_ROOM_NAME_LEN];
    client_t * members[MAX_MEMBERS];
    int member_count;
    client_t* admin_client;

    message_t history[HISTORY_SIZE];
    int history_count;       // total messages ever added (not capped)
    int history_start;       // index of oldest message in circular buffer

    pthread_mutex_t room_lock; // protects members[], history[] for this room only
} room_t;

// Presence status for a connected client
typedef enum {
    PRESENCE_ONLINE = 0,
    PRESENCE_AWAY,
    PRESENCE_BUSY
} presence_status_t;

// Rate limiting constants (token bucket)
#define RATE_BUCKET_MAX      5      // max tokens a client can hold
#define RATE_REFILL_RATE     1.0    // tokens added per second
#define RATE_MSG_COST        1      // tokens consumed per MSG or PM

// Client Definition
typedef struct client_t {
    int socket_fd;
    char client_name[MAX_USERNAME_LEN];
    room_t* current_room;
    presence_status_t status;   // online / away / busy

    // Token bucket for rate limiting (uses high-res monotonic clock)
    double          tokens;         // current token count
    struct timespec last_refill;    // last refill timestamp (nanosecond precision)
} client_t;

// Global rwlock — protects room_list[] and client_list[] arrays only.
// Per-room operations use room->room_lock instead.
extern pthread_rwlock_t registry_lock;

// Room List and Count
extern room_t *room_list[MAX_ROOMS];
extern int room_count;

extern client_t *client_list[MAX_CLIENTS];
extern int client_count;


room_t *create_room(const char *room_name, client_t *creator_client, room_err_t *err);
room_t *find_room(const char *room_name, room_err_t *err);
room_t *find_room_unlocked(const char* room_name, room_err_t *err);
void room_add_member(room_t *room, client_t *client, room_err_t *err);
void room_remove_member(room_t *room, client_t *client, room_err_t *err);
void room_broadcast(room_t *room, const char *msg, int exclude_fd);
void room_add_history(room_t *room, const char *sender, const char *text);
void room_send_history(room_t *room, int fd);
void delete_room(room_t *room, room_err_t *err);
void room_delete_if_empty(room_t *room);

client_t *create_client(int socket_fd, const char* client_name, client_err_t *err);
client_t *find_client_unlocked(const char *username, client_err_t *err);
client_t *find_client(const char *username, client_err_t *err);
void delete_client(client_t *client, client_err_t *err);
void notify_all_clients(const char *msg);


#endif