#include "../include/registry.h"
#include "../include/protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Global state
//   registry_lock (rwlock) — guards room_list[] and client_list[] arrays only.
//   Each room_t has its own room_lock (mutex) for its members[] and history[].
// ─────────────────────────────────────────────────────────────────────────────
pthread_rwlock_t registry_lock = PTHREAD_RWLOCK_INITIALIZER;

room_t   *room_list[MAX_ROOMS];
int       room_count = 0;

client_t *client_list[MAX_CLIENTS];
int       client_count = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Shift helpers
// ─────────────────────────────────────────────────────────────────────────────
static void shift_array_room(room_t *room, int start, int end)
{
    for (int i = start; i < end; i++)
        room->members[i] = room->members[i + 1];
}

static void shift_array_room_list(room_t *list[], int start, int end)
{
    for (int i = start; i < end; i++)
        list[i] = list[i + 1];
}

static void shift_array_client_list(client_t *list[], int start, int end)
{
    for (int i = start; i < end; i++)
        list[i] = list[i + 1];
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — find (unlocked, caller must hold at least rdlock on registry_lock)
// ─────────────────────────────────────────────────────────────────────────────
room_t *find_room_unlocked(const char *room_name, room_err_t *err)
{
    if (room_name == NULL || strlen(room_name) == 0 || strlen(room_name) >= MAX_ROOM_NAME_LEN)
    {
        *err = ROOM_ERR_INVALID_NAME;
        return NULL;
    }
    for (int i = 0; i < room_count; i++)
    {
        if (strcmp(room_list[i]->room_name, room_name) == 0)
        {
            *err = ROOM_OK;
            return room_list[i];
        }
    }
    *err = ROOM_ERR_NOT_FOUND;
    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — find (thread-safe, read lock only so multiple threads can find at once)
// ─────────────────────────────────────────────────────────────────────────────
room_t *find_room(const char *room_name, room_err_t *err)
{
    pthread_rwlock_rdlock(&registry_lock);
    room_t *r = find_room_unlocked(room_name, err);
    pthread_rwlock_unlock(&registry_lock);
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — create
// ─────────────────────────────────────────────────────────────────────────────
room_t *create_room(const char *room_name, client_t *creator_client, room_err_t *err)
{
    if (creator_client == NULL) { *err = ROOM_ERR_INVALID_CLIENT; return NULL; }
    if (room_name == NULL)      { *err = ROOM_ERR_INVALID_NAME;   return NULL; }

    size_t len = strlen(room_name);
    if (len == 0 || len >= MAX_ROOM_NAME_LEN) { *err = ROOM_ERR_INVALID_NAME; return NULL; }

    // Write-lock: we may add to room_list[]
    pthread_rwlock_wrlock(&registry_lock);

    if (room_count >= MAX_ROOMS)
    {
        pthread_rwlock_unlock(&registry_lock);
        *err = ROOM_ERR_MAX_ROOMS;
        return NULL;
    }

    if (find_room_unlocked(room_name, err) != NULL)
    {
        pthread_rwlock_unlock(&registry_lock);
        *err = ROOM_ERR_ALREADY_EXISTS;
        return NULL;
    }

    room_t *new_room = malloc(sizeof(room_t));
    if (!new_room)
    {
        pthread_rwlock_unlock(&registry_lock);
        *err = ROOM_ERR_ALLOC_FAILED;
        return NULL;
    }

    strncpy(new_room->room_name, room_name, MAX_ROOM_NAME_LEN);
    new_room->room_name[MAX_ROOM_NAME_LEN - 1] = '\0';
    new_room->admin_client  = creator_client;
    new_room->member_count  = 0;
    new_room->history_count = 0;
    new_room->history_start = 0;
    pthread_mutex_init(&new_room->room_lock, NULL);

    room_list[room_count++] = new_room;

    pthread_rwlock_unlock(&registry_lock);
    *err = ROOM_OK;
    return new_room;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — add member (uses per-room lock, not registry lock)
// ─────────────────────────────────────────────────────────────────────────────
void room_add_member(room_t *room, client_t *client, room_err_t *err)
{
    if (room   == NULL) { *err = ROOM_ERR_NULL;           return; }
    if (client == NULL) { *err = ROOM_ERR_INVALID_CLIENT; return; }
    if (client->current_room != NULL) { *err = ROOM_ERR_ALREADY_IN_A_ROOM; return; }

    pthread_mutex_lock(&room->room_lock);

    if (room->member_count >= MAX_MEMBERS)
    {
        pthread_mutex_unlock(&room->room_lock);
        *err = ROOM_ERR_MAX_MEMBERS;
        return;
    }

    room->members[room->member_count++] = client;

    pthread_mutex_unlock(&room->room_lock);
    *err = ROOM_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — remove member (uses per-room lock)
// ─────────────────────────────────────────────────────────────────────────────
void room_remove_member(room_t *room, client_t *client, room_err_t *err)
{
    if (room   == NULL) { *err = ROOM_ERR_NULL;           return; }
    if (client == NULL) { *err = ROOM_ERR_INVALID_CLIENT; return; }

    pthread_mutex_lock(&room->room_lock);

    int idx = -1;
    for (int i = 0; i < room->member_count; i++)
    {
        if (room->members[i] == client) { idx = i; break; }
    }

    if (idx == -1)
    {
        pthread_mutex_unlock(&room->room_lock);
        *err = ROOM_ERR_CLIENT_NOT_FOUND;
        return;
    }

    room->member_count--;
    if (idx != room->member_count)
        shift_array_room(room, idx, room->member_count);

    room->members[room->member_count] = NULL;

    pthread_mutex_unlock(&room->room_lock);
    *err = ROOM_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — broadcast (snapshot fds under per-room lock, send without any lock)
// ─────────────────────────────────────────────────────────────────────────────
void room_broadcast(room_t *room, const char *msg, int exclude_fd)
{
    if (room == NULL) return;

    int fds[MAX_MEMBERS];
    int count;

    pthread_mutex_lock(&room->room_lock);
    count = room->member_count;
    for (int i = 0; i < count; i++)
        fds[i] = room->members[i]->socket_fd;
    pthread_mutex_unlock(&room->room_lock);

    for (int i = 0; i < count; i++)
    {
        if (fds[i] == exclude_fd) continue;
        send(fds[i], msg, strlen(msg), 0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — add a message to history (circular buffer, per-room lock)
// ─────────────────────────────────────────────────────────────────────────────
void room_add_history(room_t *room, const char *sender, const char *text)
{
    if (room == NULL || sender == NULL || text == NULL) return;

    pthread_mutex_lock(&room->room_lock);

    // Slot to write into: wraps around after HISTORY_SIZE
    int slot;
    if (room->history_count < HISTORY_SIZE)
    {
        // Buffer not full yet — just append
        slot = room->history_count;
    }
    else
    {
        // Buffer full — overwrite the oldest slot and advance start
        slot = room->history_start;
        room->history_start = (room->history_start + 1) % HISTORY_SIZE;
    }

    strncpy(room->history[slot].sender, sender, MAX_USERNAME_LEN - 1);
    room->history[slot].sender[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(room->history[slot].text, text, MAX_TEXT_LEN - 1);
    room->history[slot].text[MAX_TEXT_LEN - 1] = '\0';
    room->history[slot].timestamp = time(NULL);

    if (room->history_count < HISTORY_SIZE)
        room->history_count++;

    pthread_mutex_unlock(&room->room_lock);
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — replay history to a single client fd (per-room lock, read-only)
// ─────────────────────────────────────────────────────────────────────────────
void room_send_history(room_t *room, int fd)
{
    if (room == NULL) return;

    pthread_mutex_lock(&room->room_lock);

    int count = room->history_count;
    if (count == 0)
    {
        pthread_mutex_unlock(&room->room_lock);
        return;
    }

    // Send a header line so client knows history is coming
    char header[MAX_LINE_LEN];
    snprintf(header, sizeof(header), "NOTICE history START %s\n", room->room_name);
    send(fd, header, strlen(header), 0);

    // Walk from oldest to newest
    for (int i = 0; i < count; i++)
    {
        int idx = (room->history_start + i) % HISTORY_SIZE;
        char line[MAX_LINE_LEN];
        format_msg_reply(line, sizeof(line),
                         room->history[idx].sender,
                         room->history[idx].text);
        send(fd, line, strlen(line), 0);
    }

    snprintf(header, sizeof(header), "NOTICE history END %s\n", room->room_name);
    send(fd, header, strlen(header), 0);

    pthread_mutex_unlock(&room->room_lock);
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — delete (write-lock registry, then destroy per-room lock)
// ─────────────────────────────────────────────────────────────────────────────
void delete_room(room_t *room, room_err_t *err)
{
    pthread_rwlock_wrlock(&registry_lock);

    if (room == NULL)
    {
        *err = ROOM_ERR_NULL;
        pthread_rwlock_unlock(&registry_lock);
        return;
    }

    int idx = -1;
    for (int i = 0; i < room_count; i++)
    {
        if (room_list[i] == room) { idx = i; break; }
    }

    if (idx == -1)
    {
        *err = ROOM_ERR_NOT_FOUND;
        pthread_rwlock_unlock(&registry_lock);
        return;
    }

    room_count--;
    if (idx != room_count)
        shift_array_room_list(room_list, idx, room_count);

    room_list[room_count] = NULL;
    pthread_rwlock_unlock(&registry_lock);

    pthread_mutex_destroy(&room->room_lock);
    free(room);
    *err = ROOM_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — delete only if it has no members left
// Safe to call after every room_remove_member without extra checks in callers.
// ─────────────────────────────────────────────────────────────────────────────
void room_delete_if_empty(room_t *room)
{
    if (room == NULL) return;

    // Check member count under the room's own lock
    pthread_mutex_lock(&room->room_lock);
    int empty = (room->member_count == 0);
    pthread_mutex_unlock(&room->room_lock);

    if (empty)
    {
        room_err_t err;
        delete_room(room, &err);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Client — find (unlocked, caller must hold at least rdlock on registry_lock)
// ─────────────────────────────────────────────────────────────────────────────
client_t *find_client_unlocked(const char *username, client_err_t *err)
{
    if (username == NULL) { *err = CLIENT_ERR_INVALID_NAME; return NULL; }

    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(client_list[i]->client_name, username) == 0)
        {
            *err = CLIENT_OK;
            return client_list[i];
        }
    }
    *err = CLIENT_ERR_NOT_FOUND;
    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Client — find (thread-safe, read lock only)
// ─────────────────────────────────────────────────────────────────────────────
client_t *find_client(const char *username, client_err_t *err)
{
    pthread_rwlock_rdlock(&registry_lock);
    client_t *c = find_client_unlocked(username, err);
    pthread_rwlock_unlock(&registry_lock);
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// Client — create (write-lock registry)
// ─────────────────────────────────────────────────────────────────────────────
client_t *create_client(int socket_fd, const char *client_name, client_err_t *err)
{
    size_t len = strlen(client_name);
    if (len == 0 || len >= MAX_USERNAME_LEN) { *err = CLIENT_ERR_INVALID_NAME; return NULL; }

    pthread_rwlock_wrlock(&registry_lock);

    if (client_count >= MAX_CLIENTS)
    {
        pthread_rwlock_unlock(&registry_lock);
        *err = CLIENT_ERR_MAX_CLIENTS;
        return NULL;
    }

    if (find_client_unlocked(client_name, err) != NULL)
    {
        pthread_rwlock_unlock(&registry_lock);
        *err = CLIENT_ERR_ALREADY_EXISTS;
        return NULL;
    }

    client_t *new_client = malloc(sizeof(client_t));
    if (!new_client)
    {
        pthread_rwlock_unlock(&registry_lock);
        *err = CLIENT_ERR_ALLOC_FAILED;
        return NULL;
    }

    strncpy(new_client->client_name, client_name, MAX_USERNAME_LEN);
    new_client->client_name[MAX_USERNAME_LEN - 1] = '\0';
    new_client->socket_fd    = socket_fd;
    new_client->current_room = NULL;
    new_client->status       = PRESENCE_ONLINE;

    client_list[client_count++] = new_client;

    pthread_rwlock_unlock(&registry_lock);
    *err = CLIENT_OK;
    return new_client;
}

// ─────────────────────────────────────────────────────────────────────────────
// Client — delete (write-lock registry)
// ─────────────────────────────────────────────────────────────────────────────
void delete_client(client_t *client, client_err_t *err)
{
    pthread_rwlock_wrlock(&registry_lock);

    if (client == NULL)
    {
        *err = CLIENT_ERR_NULL;
        pthread_rwlock_unlock(&registry_lock);
        return;
    }

    int idx = -1;
    for (int i = 0; i < client_count; i++)
    {
        if (client_list[i] == client) { idx = i; break; }
    }

    if (idx == -1)
    {
        *err = CLIENT_ERR_NOT_FOUND;
        pthread_rwlock_unlock(&registry_lock);
        return;
    }

    client_count--;
    if (idx != client_count)
        shift_array_client_list(client_list, idx, client_count);

    client_list[client_count] = NULL;
    free(client);

    pthread_rwlock_unlock(&registry_lock);
    *err = CLIENT_OK;
}
