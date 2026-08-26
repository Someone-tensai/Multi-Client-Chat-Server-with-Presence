#include "../include/registry.h"
#include "../include/protocol.h"
#include "../include/db.h"
#include "../include/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Global state — dynamically allocated
//   registry_lock (rwlock) — guards room_list[] and client_list[] arrays only.
//   Each room_t has its own room_lock (mutex) for its members[] and history[].
// ─────────────────────────────────────────────────────────────────────────────
pthread_rwlock_t registry_lock = PTHREAD_RWLOCK_INITIALIZER;

room_t   **room_list   = NULL;
int       room_count   = 0;
int       room_capacity = 0;

client_t **client_list = NULL;
int       client_count = 0;
int       client_capacity = 0;

// Runtime capacities for new rooms/members/history
static int g_max_members  = CFG_DEFAULT_MAX_MEMBERS;
static int g_history_size = CFG_DEFAULT_HISTORY_SIZE;

// ─────────────────────────────────────────────────────────────────────────────
// Registry lifecycle
// ─────────────────────────────────────────────────────────────────────────────
int registry_init(const server_config_t *cfg)
{
    if (!cfg) return -1;

    int need_rooms   = cfg->max_rooms   > 0 ? cfg->max_rooms   : CFG_DEFAULT_MAX_ROOMS;
    int need_clients = cfg->max_clients > 0 ? cfg->max_clients : CFG_DEFAULT_MAX_CLIENTS;
    int need_members = cfg->max_members > 0 ? cfg->max_members : CFG_DEFAULT_MAX_MEMBERS;
    int need_history = cfg->history_size >=0 ? cfg->history_size : CFG_DEFAULT_HISTORY_SIZE;

    // If already initialized, destroy previous first (useful for tests)
    if (room_list || client_list) {
        registry_destroy();
    }

    room_list = calloc((size_t)need_rooms, sizeof(room_t *));
    if (!room_list) return -1;
    client_list = calloc((size_t)need_clients, sizeof(client_t *));
    if (!client_list) {
        free(room_list);
        room_list = NULL;
        return -1;
    }

    room_capacity   = need_rooms;
    client_capacity = need_clients;
    room_count   = 0;
    client_count = 0;
    g_max_members  = need_members;
    g_history_size = need_history;

    return 0;
}

static void registry_ensure_init(void)
{
    if (room_list == NULL && client_list == NULL) {
        server_config_t def;
        def.max_rooms   = CFG_DEFAULT_MAX_ROOMS;
        def.max_clients = CFG_DEFAULT_MAX_CLIENTS;
        def.max_members = CFG_DEFAULT_MAX_MEMBERS;
        def.history_size = CFG_DEFAULT_HISTORY_SIZE;
        def.port = CFG_DEFAULT_PORT;
        def.thread_pool_size = CFG_DEFAULT_THREAD_POOL_SIZE;
        def.rate_bucket_max = CFG_DEFAULT_RATE_BUCKET_MAX;
        def.rate_refill_rate = CFG_DEFAULT_RATE_REFILL_RATE;
        def.rate_msg_cost = CFG_DEFAULT_RATE_MSG_COST;
        def.pool_shrink_idle_sec = CFG_DEFAULT_POOL_SHRINK_IDLE;
        def.pool_min_threads = CFG_DEFAULT_POOL_MIN_THREADS;
        strncpy(def.tls_cert, CFG_DEFAULT_TLS_CERT, sizeof(def.tls_cert)-1);
        strncpy(def.tls_key, CFG_DEFAULT_TLS_KEY, sizeof(def.tls_key)-1);
        registry_init(&def);
    }
}

void room_destroy(room_t *room)
{
    if (!room) return;
    pthread_mutex_destroy(&room->room_lock);
    free(room->members);
    free(room->history);
    free(room);
}

void registry_destroy(void)
{
    // Acquire write lock to ensure no other thread is accessing
    pthread_rwlock_wrlock(&registry_lock);

    // Free all rooms
    for (int i = 0; i < room_count; i++) {
        if (room_list && room_list[i]) {
            room_destroy(room_list[i]);
            room_list[i] = NULL;
        }
    }
    free(room_list);
    room_list = NULL;
    room_count = 0;
    room_capacity = 0;

    // Free all clients
    for (int i = 0; i < client_count; i++) {
        if (client_list && client_list[i]) {
            free(client_list[i]);
            client_list[i] = NULL;
        }
    }
    free(client_list);
    client_list = NULL;
    client_count = 0;
    client_capacity = 0;

    pthread_rwlock_unlock(&registry_lock);
    pthread_rwlock_destroy(&registry_lock);
    // Mark that lock was destroyed so next init will re-init it
    // Use a file-scope static to track this — we need to persist across calls
    // Since we cannot easily modify lock_destroyed from here (it's inside registry_init's scope),
    // we use a global static outside.
    // Instead, we will reinitialize lock in next registry_init unconditionally after destroy.
    // Workaround: set a global flag.
    // We introduce a separate static variable at file scope for this.
    // For now, we just leave lock destroyed; next registry_init will need to init it.
    // To make it work, we set a flag in a way registry_init can detect: if room_list==NULL && client_list==NULL means destroyed.
    // registry_init will check if registry_lock needs init by trying to re-init.
    // Simpler: just re-init lock now to keep it usable for subsequent tests without re-init?
    // But if we destroy, next test will call registry_init which will try to destroy again — not good.
    // So we re-init immediately after destroy to keep lock valid for next init's destroy path.
    pthread_rwlock_init(&registry_lock, NULL);
}

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
        if (room_list[i] && strcmp(room_list[i]->room_name, room_name) == 0)
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

    registry_ensure_init();

    // Write-lock: we may add to room_list[]
    pthread_rwlock_wrlock(&registry_lock);

    if (room_capacity == 0 || room_list == NULL) {
        pthread_rwlock_unlock(&registry_lock);
        *err = ROOM_ERR_MAX_ROOMS;
        return NULL;
    }

    if (room_count >= room_capacity)
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

    room_t *new_room = calloc(1, sizeof(room_t));
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
    new_room->member_capacity = g_max_members;
    new_room->history_count = 0;
    new_room->history_start = 0;
    new_room->history_capacity = g_history_size;

    // Allocate members array
    if (new_room->member_capacity > 0) {
        new_room->members = calloc((size_t)new_room->member_capacity, sizeof(client_t *));
        if (!new_room->members) {
            free(new_room);
            pthread_rwlock_unlock(&registry_lock);
            *err = ROOM_ERR_ALLOC_FAILED;
            return NULL;
        }
    } else {
        new_room->members = NULL;
    }

    // Allocate history array
    if (new_room->history_capacity > 0) {
        new_room->history = calloc((size_t)new_room->history_capacity, sizeof(message_t));
        if (!new_room->history) {
            free(new_room->members);
            free(new_room);
            pthread_rwlock_unlock(&registry_lock);
            *err = ROOM_ERR_ALLOC_FAILED;
            return NULL;
        }
    } else {
        new_room->history = NULL;
    }

    if (pthread_mutex_init(&new_room->room_lock, NULL) != 0) {
        free(new_room->members);
        free(new_room->history);
        free(new_room);
        pthread_rwlock_unlock(&registry_lock);
        *err = ROOM_ERR_ALLOC_FAILED;
        return NULL;
    }

    // Load prior message history from DB into the in-memory circular buffer
    if (new_room->history_capacity > 0) {
        db_message_t *rows = malloc(sizeof(db_message_t) * (size_t)new_room->history_capacity);
        if (rows) {
            int loaded = db_load_history(room_name, rows, new_room->history_capacity);
            for (int i = 0; i < loaded; i++)
            {
                int cap = new_room->history_capacity;
                int slot;
                if (new_room->history_count < cap)
                    slot = new_room->history_count;
                else {
                    slot = new_room->history_start;
                    new_room->history_start = (new_room->history_start + 1) % cap;
                }

                strncpy(new_room->history[slot].sender, rows[i].sender, MAX_USERNAME_LEN - 1);
                new_room->history[slot].sender[MAX_USERNAME_LEN - 1] = '\0';
                strncpy(new_room->history[slot].text, rows[i].text, MAX_TEXT_LEN - 1);
                new_room->history[slot].text[MAX_TEXT_LEN - 1] = '\0';
                new_room->history[slot].timestamp = (time_t)rows[i].timestamp;

                if (new_room->history_count < cap)
                    new_room->history_count++;
                else {
                    // history_count is total, keep incrementing? But loaded case is capped.
                    // For loaded history, we keep capped count (not total) to avoid overflow on replay.
                    // If we treat history_count as total, loaded should reflect stored count (capped).
                    // We keep capped for now to match replay logic.
                }
            }
            free(rows);
        }
    }

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

    if (room->member_count >= room->member_capacity)
    {
        pthread_mutex_unlock(&room->room_lock);
        *err = ROOM_ERR_MAX_MEMBERS;
        return;
    }

    if (!room->members) {
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

    if (room->members)
        room->members[room->member_count] = NULL;

    pthread_mutex_unlock(&room->room_lock);
    *err = ROOM_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — broadcast (snapshot fds under per-room lock, send without any lock)
// ─────────────────────────────────────────────────────────────────────────────
void room_broadcast(room_t *room, const char *msg, int exclude_fd)
{
    if (room == NULL || msg == NULL) return;

    int count;
    int *fds = NULL;

    pthread_mutex_lock(&room->room_lock);
    count = room->member_count;
    if (count > 0 && room->members) {
        fds = malloc(sizeof(int) * (size_t)count);
        if (fds) {
            for (int i = 0; i < count; i++)
                fds[i] = room->members[i]->socket_fd;
        } else {
            count = 0;
        }
    } else {
        count = 0;
    }
    pthread_mutex_unlock(&room->room_lock);

    if (!fds) return;

    for (int i = 0; i < count; i++)
    {
        if (fds[i] == exclude_fd) continue;
        send(fds[i], msg, strlen(msg), MSG_NOSIGNAL);
    }
    free(fds);
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — add a message to history (circular buffer, per-room lock)
// ─────────────────────────────────────────────────────────────────────────────
void room_add_history(room_t *room, const char *sender, const char *text)
{
    if (room == NULL || sender == NULL || text == NULL) return;

    pthread_mutex_lock(&room->room_lock);

    int cap = room->history_capacity;
    if (cap <= 0 || !room->history) {
        pthread_mutex_unlock(&room->room_lock);
        db_save_message(room->room_name, sender, text);
        return;
    }

    int slot;
    if (room->history_count < cap)
    {
        slot = room->history_count;
    }
    else
    {
        slot = room->history_start;
        room->history_start = (room->history_start + 1) % cap;
    }

    strncpy(room->history[slot].sender, sender, MAX_USERNAME_LEN - 1);
    room->history[slot].sender[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(room->history[slot].text, text, MAX_TEXT_LEN - 1);
    room->history[slot].text[MAX_TEXT_LEN - 1] = '\0';
    room->history[slot].timestamp = time(NULL);

    // history_count tracks total messages (capped for replay compatibility we also handle via min)
    // We increment total, but for replay we use min(total, cap)
    // To keep backward compat where history_count was capped, we still increment only until cap then keep tracking total via separate logic?
    // Spec says history_count is total, so we increment always.
    // However to avoid breaking existing replay that expects capped, we store total and replay uses min.
    if (room->history_count < 1000000) // avoid overflow
        room->history_count++;
    else {
        // If overflow, wrap? Keep at cap
        room->history_count = cap;
    }
    // For backwards compat, if we want capped behavior, we could cap history_count at cap.
    // But we want total, so we keep total. To keep replay correct, room_send_history uses min(total, cap).
    // However if total grows large, history_count will exceed cap, but replay still works.

    pthread_mutex_unlock(&room->room_lock);

    // Persist to database so history survives server restarts
    db_save_message(room->room_name, sender, text);
}

// ─────────────────────────────────────────────────────────────────────────────
// Room — replay history to a single client fd (per-room lock, read-only)
// ─────────────────────────────────────────────────────────────────────────────
void room_send_history(room_t *room, int fd)
{
    if (room == NULL) return;

    pthread_mutex_lock(&room->room_lock);

    int cap = room->history_capacity;
    if (cap <= 0 || !room->history) {
        pthread_mutex_unlock(&room->room_lock);
        return;
    }

    int total = room->history_count;
    int stored = total < cap ? total : cap;
    if (stored == 0)
    {
        pthread_mutex_unlock(&room->room_lock);
        return;
    }

    // Send a header line so client knows history is coming
    char header[MAX_LINE_LEN];
    snprintf(header, sizeof(header), "NOTICE history START %s\n", room->room_name);
    send(fd, header, strlen(header), MSG_NOSIGNAL);

    // Walk from oldest to newest
    // When total < cap, start is 0, so idx = i
    // When total >= cap, start points to oldest, so idx = (start + i) % cap
    for (int i = 0; i < stored; i++)
    {
        int idx;
        if (total < cap)
            idx = i;
        else
            idx = (room->history_start + i) % cap;
        char line[MAX_LINE_LEN];
        format_msg_reply(line, sizeof(line),
                         room->history[idx].sender,
                         room->history[idx].text);
        send(fd, line, strlen(line), MSG_NOSIGNAL);
    }

    snprintf(header, sizeof(header), "NOTICE history END %s\n", room->room_name);
    send(fd, header, strlen(header), MSG_NOSIGNAL);

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

    room_destroy(room);
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
// Broadcast a raw message to every connected client (used for server shutdown)
// ─────────────────────────────────────────────────────────────────────────────
void notify_all_clients(const char *msg)
{
    if (!msg) return;
    int count;
    int *fds = NULL;
    pthread_rwlock_rdlock(&registry_lock);
    count = client_count;
    if (count > 0 && client_list) {
        fds = malloc(sizeof(int) * (size_t)count);
        if (fds) {
            for (int i = 0; i < count; i++)
                fds[i] = client_list[i]->socket_fd;
        } else {
            count = 0;
        }
    } else {
        count = 0;
    }
    pthread_rwlock_unlock(&registry_lock);

    if (!fds) return;
    for (int i = 0; i < count; i++)
        send(fds[i], msg, strlen(msg), MSG_NOSIGNAL);
    free(fds);
}

// ─────────────────────────────────────────────────────────────────────────────
// Client — find (unlocked, caller must hold at least rdlock on registry_lock)
// ─────────────────────────────────────────────────────────────────────────────
client_t *find_client_unlocked(const char *username, client_err_t *err)
{
    if (username == NULL) { *err = CLIENT_ERR_INVALID_NAME; return NULL; }

    for (int i = 0; i < client_count; i++)
    {
        if (client_list[i] && strcmp(client_list[i]->client_name, username) == 0)
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

    registry_ensure_init();

    pthread_rwlock_wrlock(&registry_lock);

    if (client_capacity == 0 || client_list == NULL) {
        pthread_rwlock_unlock(&registry_lock);
        *err = CLIENT_ERR_MAX_CLIENTS;
        return NULL;
    }

    if (client_count >= client_capacity)
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
    new_client->tokens       = RATE_BUCKET_MAX;
    clock_gettime(CLOCK_MONOTONIC, &new_client->last_refill);

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
