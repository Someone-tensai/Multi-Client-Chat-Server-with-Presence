#include "../include/server.h"
#include "../include/registry.h"
#include "../include/protocol.h"
#include "../include/db.h"
#include "../include/log.h"
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────────────────
// Forward declare epoll_rearm (defined in server.c)
// ─────────────────────────────────────────────────────────────────────────────
void epoll_rearm(conn_t *conn);

// ─────────────────────────────────────────────────────────────────────────────
// Token bucket rate limiter
// ─────────────────────────────────────────────────────────────────────────────
static int rate_limit_check(client_t *client)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed = (now.tv_sec  - client->last_refill.tv_sec) +
                     (now.tv_nsec - client->last_refill.tv_nsec) / 1e9;

    client->tokens += elapsed * RATE_REFILL_RATE;
    if (client->tokens > RATE_BUCKET_MAX)
        client->tokens = RATE_BUCKET_MAX;
    client->last_refill = now;

    if (client->tokens < RATE_MSG_COST)
        return 0;

    client->tokens -= RATE_MSG_COST;
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: create client even if username already active (for reconnect/login force)
// Used to allow new connection to replace old when duplicate exists.
// Returns new client or NULL on failure.
// ─────────────────────────────────────────────────────────────────────────────
static client_t *create_client_force(int socket_fd, const char *client_name, client_err_t *err)
{
    size_t len = strlen(client_name);
    if (len == 0 || len >= MAX_USERNAME_LEN) { *err = CLIENT_ERR_INVALID_NAME; return NULL; }

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

    // Intentionally skip duplicate check

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

static void evict_old_client_if_exists(const char *username, int new_fd)
{
    (void)new_fd;
    // This is a best-effort eviction: if an old client with same username exists,
    // we will close its socket and remove it from its room, but we keep its
    // client object alive until its conn disconnects (to avoid dangling conn->me).
    // Instead, we will just leave old client in list and allow duplicate.
    // For now, we just log and allow duplicate; old will be cleaned on its own disconnect.
    // No immediate action needed — duplicate handling is done via create_client_force.
    LOG_INFO("Duplicate login for user %s — allowing new connection (old will be cleaned on disconnect)", username);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cleanup — called when the client disconnects cleanly or on error.
// Mirrors the teardown at the bottom of the old blocking handle_client.
// ─────────────────────────────────────────────────────────────────────────────
static void disconnect_cleanup(conn_t *conn)
{
    client_t *me = conn->me;
    char reply[MAX_LINE_LEN];
    room_err_t room_err;

    if (me != NULL)
    {
        if (me->current_room != NULL)
        {
            room_t *last_room = me->current_room;
            room_remove_member(last_room, me, &room_err);
            me->current_room = NULL;

            format_notice(reply, sizeof(reply), me->client_name, OK_LEFT, last_room->room_name);
            room_broadcast(last_room, reply, conn->fd);

            room_delete_if_empty(last_room);
        }

        client_err_t cleanup_err;
        delete_client(me, &cleanup_err);
        conn->me = NULL;
        LOG_INFO("Client %s disconnected (fd=%d)", me->client_name, conn->fd);
    } else {
        LOG_DEBUG("Anonymous client disconnected (fd=%d)", conn->fd);
    }

    close(conn->fd);
    conn_free(conn);
}

// ─────────────────────────────────────────────────────────────────────────────
// process_line — run one complete command line through the switch.
// `line` is null-terminated and already stripped of \r\n.
// Returns 1 if the connection should be closed after this line, 0 otherwise.
// ─────────────────────────────────────────────────────────────────────────────
static int process_line(conn_t *conn, char *line)
{
    client_t      *me       = conn->me;
    int            client_fd = conn->fd;
    char           reply[MAX_LINE_LEN];
    client_err_t   client_err;
    room_err_t     room_err;
    room_t        *already_in;

    cmd command = parse_incoming_command_server(line);

    if (me == NULL && command.type != TYPE_REGISTER && command.type != TYPE_LOGIN && command.type != TYPE_RECONNECT)
    {
        format_err_reply(reply, sizeof(reply), ERR_NOT_REGISTERED);
        conn_send(conn, reply, strlen(reply));
        return 0;
    }

    switch (command.type)
    {
        case TYPE_INVALID:
        {
            format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
            conn_send(conn, reply, strlen(reply));
            break;
        }

        case TYPE_REGISTER:
        {
            if (me != NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_ALREADY_REGISTERED);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            char *username = command.arg1;
            char *password = command.arg2;
            if (username == NULL || password == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            int db_rc = db_register_user(username, password);
            if (db_rc == -1)
            {
                format_err_reply(reply, sizeof(reply), ERR_USERNAME_TAKEN);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (db_rc == -2)
            {
                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            // Check if already active (should not happen after DB check, but handle)
            client_err_t dup_check;
            client_t *existing = find_client(username, &dup_check);
            if (existing) {
                format_err_reply(reply, sizeof(reply), ERR_USERNAME_TAKEN);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            conn->me = create_client(client_fd, username, &client_err);
            me = conn->me;

            switch (client_err)
            {
                case CLIENT_OK: {
                    // Create session token
                    char token[65];
                    if (db_create_session(username, token, sizeof(token)) == 0) {
                        LOG_INFO("User %s registered, session created", username);
                        format_ok_session(reply, sizeof(reply), OK_REGISTERED, token);
                    } else {
                        LOG_WARN("Session creation failed for %s", username);
                        format_ok_reply(reply, sizeof(reply), OK_REGISTERED);
                    }
                    conn_send(conn, reply, strlen(reply));
                    break;
                }
                case CLIENT_ERR_ALLOC_FAILED:
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case CLIENT_ERR_ALREADY_EXISTS:
                    format_err_reply(reply, sizeof(reply), ERR_USERNAME_TAKEN);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case CLIENT_ERR_INVALID_NAME:
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_USERNAME);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case CLIENT_ERR_MAX_CLIENTS:
                    format_err_reply(reply, sizeof(reply), ERR_MAX_CLIENT_COUNT_REACHED);
                    conn_send(conn, reply, strlen(reply));
                    break;
                default:
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    conn_send(conn, reply, strlen(reply));
                    break;
            }
            break;
        }

        case TYPE_LOGIN:
        {
            if (me != NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_ALREADY_REGISTERED);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            char *username = command.arg1;
            char *password = command.arg2;
            if (username == NULL || password == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            int verify = db_verify_user(username, password);
            if (verify == -1)
            {
                format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (verify == -2)
            {
                format_err_reply(reply, sizeof(reply), ERR_WRONG_PASSWORD);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (verify == -3)
            {
                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            // Handle duplicate active session: allow new login to replace old
            client_err_t dup_check;
            client_t *existing = find_client(username, &dup_check);
            if (existing) {
                evict_old_client_if_exists(username, client_fd);
                // Use force create to allow duplicate username temporarily
                conn->me = create_client_force(client_fd, username, &client_err);
            } else {
                conn->me = create_client(client_fd, username, &client_err);
            }
            me = conn->me;

            switch (client_err)
            {
                case CLIENT_OK: {
                    char token[65];
                    if (db_create_session(username, token, sizeof(token)) == 0) {
                        LOG_INFO("User %s logged in, session created", username);
                        format_ok_session(reply, sizeof(reply), OK_LOGGED_IN, token);
                    } else {
                        LOG_WARN("Session creation failed for %s", username);
                        format_ok_reply(reply, sizeof(reply), OK_LOGGED_IN);
                    }
                    conn_send(conn, reply, strlen(reply));
                    break;
                }
                case CLIENT_ERR_ALREADY_EXISTS:
                    format_err_reply(reply, sizeof(reply), ERR_USERNAME_TAKEN);
                    conn_send(conn, reply, strlen(reply));
                    conn->me = NULL;
                    me = NULL;
                    break;
                case CLIENT_ERR_MAX_CLIENTS:
                    format_err_reply(reply, sizeof(reply), ERR_MAX_CLIENT_COUNT_REACHED);
                    conn_send(conn, reply, strlen(reply));
                    conn->me = NULL;
                    me = NULL;
                    break;
                default:
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    conn_send(conn, reply, strlen(reply));
                    conn->me = NULL;
                    me = NULL;
                    break;
            }
            break;
        }

        case TYPE_RECONNECT:
        {
            if (me != NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_ALREADY_REGISTERED);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            char *token = command.arg1;
            if (token == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            // Cleanup expired sessions periodically
            db_cleanup_expired_sessions();

            char username[MAX_USERNAME_LEN];
            int valid = db_validate_session(token, username, sizeof(username));
            if (valid == -1)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_TOKEN);
                conn_send(conn, reply, strlen(reply));
                LOG_INFO("Reconnect failed: invalid token");
                break;
            }
            if (valid == -2)
            {
                format_err_reply(reply, sizeof(reply), ERR_SESSION_EXPIRED);
                conn_send(conn, reply, strlen(reply));
                LOG_INFO("Reconnect failed: expired token");
                break;
            }
            if (valid != 0)
            {
                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            // Token valid — verify user still exists
            if (!db_user_exists(username))
            {
                format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                conn_send(conn, reply, strlen(reply));
                db_delete_session(token);
                break;
            }

            // Handle duplicate active client (allow reconnect to replace)
            client_err_t dup_check;
            client_t *existing = find_client(username, &dup_check);
            if (existing) {
                evict_old_client_if_exists(username, client_fd);
                conn->me = create_client_force(client_fd, username, &client_err);
            } else {
                conn->me = create_client(client_fd, username, &client_err);
            }
            me = conn->me;

            if (client_err != CLIENT_OK)
            {
                if (client_err == CLIENT_ERR_MAX_CLIENTS)
                    format_err_reply(reply, sizeof(reply), ERR_MAX_CLIENT_COUNT_REACHED);
                else
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                conn_send(conn, reply, strlen(reply));
                conn->me = NULL;
                me = NULL;
                break;
            }

            // Rotate token: delete old, create new
            db_delete_session(token);
            char new_token[65];
            if (db_create_session(username, new_token, sizeof(new_token)) != 0) {
                LOG_WARN("Token rotation failed for %s", username);
                // Still succeed with old token's username, but without new token
                format_ok_reply(reply, sizeof(reply), OK_RECONNECTED);
                conn_send(conn, reply, strlen(reply));
            } else {
                LOG_INFO("User %s reconnected, token rotated", username);
                format_ok_session(reply, sizeof(reply), OK_RECONNECTED, new_token);
                conn_send(conn, reply, strlen(reply));
            }
            break;
        }

        case TYPE_CREATE:
        {
            char *room_name = command.arg1;
            if (room_name == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            room_t *new_room = create_room(room_name, me, &room_err);

            switch (room_err)
            {
                case ROOM_OK:
                    room_add_member(new_room, me, &room_err);
                    switch (room_err)
                    {
                        case ROOM_OK:
                            me->current_room = new_room;
                            format_ok_reply(reply, sizeof(reply), OK_CREATED);
                            conn_send(conn, reply, strlen(reply));
                            LOG_INFO("User %s created room %s", me->client_name, room_name);
                            break;
                        case ROOM_ERR_NULL:
                            format_err_reply(reply, sizeof(reply), ERR_ROOM_NULL);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        case ROOM_ERR_INVALID_CLIENT:
                            format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        case ROOM_ERR_MAX_MEMBERS:
                            format_err_reply(reply, sizeof(reply), ERR_ROOM_MAX_MEMBER_COUNT_REACHED);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        default:
                            format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                            conn_send(conn, reply, strlen(reply));
                            break;
                    }
                    break;
                case ROOM_ERR_ALLOC_FAILED:
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case ROOM_ERR_ALREADY_EXISTS:
                    format_err_reply(reply, sizeof(reply), ERR_ROOM_ALREADY_EXISTS);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case ROOM_ERR_INVALID_CLIENT:
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case ROOM_ERR_INVALID_NAME:
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case ROOM_ERR_MAX_ROOMS:
                    format_err_reply(reply, sizeof(reply), ERR_MAX_ROOM_COUNT_REACHED);
                    conn_send(conn, reply, strlen(reply));
                    break;
                default:
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    conn_send(conn, reply, strlen(reply));
                    break;
            }
            break;
        }

        case TYPE_JOIN:
        {
            char *room_name = command.arg1;
            if (room_name == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            if (me->current_room != NULL)
            {
                already_in = me->current_room;
                room_remove_member(already_in, me, &room_err);
                switch (room_err)
                {
                    case ROOM_OK:
                        me->current_room = NULL;
                        break;
                    case ROOM_ERR_NULL:
                        format_err_reply(reply, sizeof(reply), ERR_ROOM_NULL);
                        conn_send(conn, reply, strlen(reply));
                        break;
                    case ROOM_ERR_INVALID_CLIENT:
                        format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                        conn_send(conn, reply, strlen(reply));
                        break;
                    case ROOM_ERR_CLIENT_NOT_FOUND:
                        format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                        conn_send(conn, reply, strlen(reply));
                        break;
                    default:
                        format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                        conn_send(conn, reply, strlen(reply));
                        break;
                }
            }

            room_t *room_found = find_room(room_name, &room_err);
            switch (room_err)
            {
                case ROOM_OK:
                    room_add_member(room_found, me, &room_err);
                    switch (room_err)
                    {
                        case ROOM_OK:
                            me->current_room = room_found;
                            format_ok_reply(reply, sizeof(reply), OK_JOINED);
                            conn_send(conn, reply, strlen(reply));
                            room_send_history(room_found, client_fd);
                            format_notice(reply, sizeof(reply), me->client_name, OK_JOINED, room_found->room_name);
                            room_broadcast(room_found, reply, conn->fd);
                            LOG_INFO("User %s joined room %s", me->client_name, room_found->room_name);
                            break;
                        case ROOM_ERR_NULL:
                            format_err_reply(reply, sizeof(reply), ERR_ROOM_NULL);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        case ROOM_ERR_INVALID_CLIENT:
                            format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        case ROOM_ERR_MAX_MEMBERS:
                            format_err_reply(reply, sizeof(reply), ERR_ROOM_MAX_MEMBER_COUNT_REACHED);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        case ROOM_ERR_ALREADY_IN_A_ROOM:
                            format_err_reply(reply, sizeof(reply), ERR_ALREADY_IN_A_ROOM);
                            conn_send(conn, reply, strlen(reply));
                            break;
                        default:
                            format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                            conn_send(conn, reply, strlen(reply));
                            break;
                    }
                    break;
                case ROOM_ERR_NOT_FOUND:
                    format_err_reply(reply, sizeof(reply), ERR_ROOM_NOT_FOUND);
                    conn_send(conn, reply, strlen(reply));
                    break;
                case ROOM_ERR_INVALID_NAME:
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                    conn_send(conn, reply, strlen(reply));
                    break;
                default:
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    conn_send(conn, reply, strlen(reply));
                    break;
            }
            break;
        }

        case TYPE_LEAVE:
        {
            if (me->current_room == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            room_t *leaving_room = me->current_room;
            room_remove_member(leaving_room, me, &room_err);
            if (room_err != ROOM_OK)
            {
                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            me->current_room = NULL;

            format_ok_reply(reply, sizeof(reply), OK_LEFT);
            conn_send(conn, reply, strlen(reply));

            format_notice(reply, sizeof(reply), me->client_name, OK_LEFT, leaving_room->room_name);
            room_broadcast(leaving_room, reply, client_fd);

            room_delete_if_empty(leaving_room);
            LOG_INFO("User %s left room %s", me->client_name, leaving_room->room_name);
            break;
        }

        case TYPE_KICK:
        {
            if (me->current_room == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (me->current_room->admin_client != me)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_ADMIN);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            char *target_name = command.arg1;
            if (target_name == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (strcmp(target_name, me->client_name) == 0)
            {
                format_err_reply(reply, sizeof(reply), ERR_CANNOT_KICK_SELF);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            client_err_t kick_err;
            client_t *target = find_client(target_name, &kick_err);
            if (target == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (target->current_room != me->current_room)
            {
                format_err_reply(reply, sizeof(reply), ERR_USER_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            room_t *kicked_from = me->current_room;
            room_remove_member(kicked_from, target, &room_err);
            if (room_err != ROOM_OK)
            {
                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            target->current_room = NULL;

            format_err_reply(reply, sizeof(reply), OK_KICKED);
            send(target->socket_fd, reply, strlen(reply), MSG_NOSIGNAL);

            format_notice(reply, sizeof(reply), target_name, OK_KICKED, kicked_from->room_name);
            room_broadcast(kicked_from, reply, target->socket_fd);

            format_ok_reply(reply, sizeof(reply), OK_KICKED);
            conn_send(conn, reply, strlen(reply));

            room_delete_if_empty(kicked_from);
            LOG_INFO("User %s kicked %s from %s", me->client_name, target_name, kicked_from->room_name);
            break;
        }

        case TYPE_PROMOTE:
        {
            if (me->current_room == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (me->current_room->admin_client != me)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_ADMIN);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            char *target_name = command.arg1;
            if (target_name == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            client_err_t promo_err;
            client_t *target = find_client(target_name, &promo_err);
            if (target == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (target->current_room != me->current_room)
            {
                format_err_reply(reply, sizeof(reply), ERR_USER_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            me->current_room->admin_client = target;

            format_ok_reply(reply, sizeof(reply), OK_PROMOTED);
            send(target->socket_fd, reply, strlen(reply), MSG_NOSIGNAL);

            format_notice(reply, sizeof(reply), target_name, OK_PROMOTED, me->current_room->room_name);
            room_broadcast(me->current_room, reply, -1);
            LOG_INFO("User %s promoted %s in %s", me->client_name, target_name, me->current_room->room_name);
            break;
        }

        case TYPE_MSG:
        {
            if (me->current_room == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (!rate_limit_check(me))
            {
                format_err_reply(reply, sizeof(reply), ERR_RATE_LIMITED);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            char *text = command.arg1;
            if (text == NULL || strlen(text) == 0)
            {
                format_err_reply(reply, sizeof(reply), ERR_EMPTY_MESSAGE);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            long long msg_id = room_add_history(me->current_room, me->client_name, text);
            if (msg_id > 0) {
                format_msg_reply_id(reply, sizeof(reply), msg_id, me->client_name, text);
            } else {
                format_msg_reply(reply, sizeof(reply), me->client_name, text);
            }
            room_broadcast(me->current_room, reply, client_fd);

            format_ok_reply(reply, sizeof(reply), OK_SENT);
            conn_send(conn, reply, strlen(reply));
            LOG_DEBUG("MSG from %s in %s id=%lld", me->client_name, me->current_room->room_name, msg_id);
            break;
        }

        case TYPE_EDIT:
        {
            if (me->current_room == NULL) {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            char *id_str = command.arg1;
            char *new_text = command.arg2;
            if (id_str == NULL || new_text == NULL || strlen(new_text)==0) {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            long long msg_id = atoll(id_str);
            if (msg_id <=0) {
                format_err_reply(reply, sizeof(reply), ERR_MSG_NOT_FOUND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            int rc = room_edit_message(me->current_room, msg_id, me->client_name, new_text);
            if (rc == -1) {
                format_err_reply(reply, sizeof(reply), ERR_MSG_NOT_FOUND);
                conn_send(conn, reply, strlen(reply));
                break;
            } else if (rc == -2) {
                format_err_reply(reply, sizeof(reply), ERR_MSG_DELETED);
                conn_send(conn, reply, strlen(reply));
                break;
            } else if (rc == -3) {
                format_err_reply(reply, sizeof(reply), ERR_NOT_AUTHORIZED);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            format_edited_reply(reply, sizeof(reply), msg_id, new_text);
            room_broadcast(me->current_room, reply, -1);
            format_ok_reply(reply, sizeof(reply), OK_SENT);
            conn_send(conn, reply, strlen(reply));
            LOG_INFO("User %s edited msg %lld in %s", me->client_name, msg_id, me->current_room->room_name);
            break;
        }

        case TYPE_DELETE:
        {
            if (me->current_room == NULL) {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            char *id_str = command.arg1;
            if (id_str == NULL) {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            long long msg_id = atoll(id_str);
            if (msg_id <=0) {
                format_err_reply(reply, sizeof(reply), ERR_MSG_NOT_FOUND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            int is_admin = (me->current_room->admin_client == me);
            int rc = room_delete_message(me->current_room, msg_id, me->client_name, is_admin);
            if (rc == -1) {
                format_err_reply(reply, sizeof(reply), ERR_MSG_NOT_FOUND);
                conn_send(conn, reply, strlen(reply));
                break;
            } else if (rc == -2) {
                format_err_reply(reply, sizeof(reply), ERR_MSG_DELETED);
                conn_send(conn, reply, strlen(reply));
                break;
            } else if (rc == -3) {
                format_err_reply(reply, sizeof(reply), ERR_NOT_AUTHORIZED);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            format_deleted_reply(reply, sizeof(reply), msg_id);
            room_broadcast(me->current_room, reply, -1);
            format_ok_reply(reply, sizeof(reply), OK_SENT);
            conn_send(conn, reply, strlen(reply));
            LOG_INFO("User %s deleted msg %lld in %s", me->client_name, msg_id, me->current_room->room_name);
            break;
        }

        case TYPE_HISTORY:
        {
            // HISTORY <room> [cursor] [limit] — cursor is message_id, 0 means latest
            char *room_name = command.arg1;
            char *cursor_str = command.arg2;
            char *limit_str = command.arg3;
            // Also handle case where HISTORY was called as HISTORY room cursor limit but our parser put cursor+limit in arg2? Now we have 3 args properly
            if (room_name == NULL) {
                // If no room specified, use current room
                if (me->current_room) room_name = me->current_room->room_name;
                else {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                    conn_send(conn, reply, strlen(reply));
                    break;
                }
            }
            long long cursor = 0;
            int limit = 20;
            if (cursor_str) cursor = atoll(cursor_str);
            if (limit_str) limit = atoi(limit_str);
            // Handle case where user did HISTORY room limit (without cursor) — treat second arg as limit if room specified and no cursor?
            // We keep simple: if only 2 args and second is numeric and room specified, treat as limit if cursor is small?
            // For now, require explicit cursor if limit provided.
            if (limit <=0) limit = 20;
            if (limit > 100) limit = 100;
            room_t *room = NULL;
            room_err_t rerr;
            room = find_room(room_name, &rerr);
            if (!room) {
                format_err_reply(reply, sizeof(reply), ERR_ROOM_NOT_FOUND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            db_message_t hist[100];
            long long next_cursor = 0;
            int count = db_load_history_before(room_name, cursor, limit, hist, &next_cursor);
            // Send header
            char hdr[MAX_LINE_LEN];
            snprintf(hdr, sizeof(hdr), "%s %s %lld %d\n", REPLY_HISTORY, room_name, next_cursor, count);
            conn_send(conn, hdr, strlen(hdr));
            for (int i=0;i<count;i++) {
                char line[MAX_LINE_LEN];
                // Use id + sender + text, include deleted marker
                if (hist[i].deleted) {
                    snprintf(line, sizeof(line), "%s %lld %s [deleted]\n", REPLY_MSG, hist[i].id, hist[i].sender);
                } else {
                    // Use id format for history
                    snprintf(line, sizeof(line), "%s %lld %s %s\n", REPLY_MSG, hist[i].id, hist[i].sender, hist[i].text);
                }
                conn_send(conn, line, strlen(line));
            }
            snprintf(hdr, sizeof(hdr), "END_HISTORY %s %lld\n", room_name, next_cursor);
            conn_send(conn, hdr, strlen(hdr));
            break;
        }

        case TYPE_PM:
        {
            char *target_name = command.arg1;
            char *text        = command.arg2;

            if (target_name == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (text == NULL || strlen(text) == 0)
            {
                format_err_reply(reply, sizeof(reply), ERR_EMPTY_MESSAGE);
                conn_send(conn, reply, strlen(reply));
                break;
            }
            if (!rate_limit_check(me))
            {
                format_err_reply(reply, sizeof(reply), ERR_RATE_LIMITED);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            client_err_t pm_err;
            client_t *target = find_client(target_name, &pm_err);
            if (target == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            format_pm_reply(reply, sizeof(reply), me->client_name, text);
            send(target->socket_fd, reply, strlen(reply), MSG_NOSIGNAL);

            format_ok_reply(reply, sizeof(reply), OK_SENT);
            conn_send(conn, reply, strlen(reply));
            LOG_DEBUG("PM from %s to %s", me->client_name, target_name);
            break;
        }

        case TYPE_ROOMS:
        {
            // Pagination: ROOMS [offset] [limit]
            int offset = 0;
            int limit = -1; // -1 means no limit (all)
            if (command.arg1) {
                offset = atoi(command.arg1);
                if (offset < 0) offset = 0;
            }
            if (command.arg2) {
                limit = atoi(command.arg2);
                if (limit < 0) limit = 0;
                if (limit > 1000) limit = 1000; // cap excessively large limit
            }

            char rooms_buf[MAX_LINE_LEN];
            // New format includes pagination info: ROOMS_REPLY <total> <offset> <count> [room...]
            // For backwards compat, if no pagination args, send old format: ROOMS_REPLY room1 room2...
            int use_pagination = (command.arg1 != NULL || command.arg2 != NULL);

            if (use_pagination) {
                pthread_rwlock_rdlock(&registry_lock);
                int total = room_count;
                if (offset > total) offset = total;
                int remaining = total - offset;
                int count = 0;
                if (limit < 0) count = remaining;
                else count = (limit < remaining) ? limit : remaining;

                int off = snprintf(rooms_buf, sizeof(rooms_buf), "%s %d %d %d", REPLY_ROOMS, total, offset, count);
                for (int i = offset; i < offset + count; i++) {
                    off += snprintf(rooms_buf + off, sizeof(rooms_buf) - off, " %s", room_list[i]->room_name);
                    if (off >= (int)sizeof(rooms_buf) - 32) break; // avoid overflow
                }
                pthread_rwlock_unlock(&registry_lock);
                snprintf(rooms_buf + off, sizeof(rooms_buf) - off, "\n");
                conn_send(conn, rooms_buf, strlen(rooms_buf));
            } else {
                int off = snprintf(rooms_buf, sizeof(rooms_buf), "%s", REPLY_ROOMS);
                pthread_rwlock_rdlock(&registry_lock);
                for (int i = 0; i < room_count; i++)
                    off += snprintf(rooms_buf + off, sizeof(rooms_buf) - off, " %s", room_list[i]->room_name);
                pthread_rwlock_unlock(&registry_lock);
                snprintf(rooms_buf + off, sizeof(rooms_buf) - off, "\n");
                conn_send(conn, rooms_buf, strlen(rooms_buf));
            }
            break;
        }

        case TYPE_STATUS:
        {
            char *new_status = command.arg1;
            if (new_status == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            presence_status_t prev = me->status;
            const char *status_str;

            if (strcmp(new_status, STATUS_ONLINE) == 0)
            {
                me->status = PRESENCE_ONLINE;
                status_str = STATUS_ONLINE;
            }
            else if (strcmp(new_status, STATUS_AWAY) == 0)
            {
                me->status = PRESENCE_AWAY;
                status_str = STATUS_AWAY;
            }
            else if (strcmp(new_status, STATUS_BUSY) == 0)
            {
                me->status = PRESENCE_BUSY;
                status_str = STATUS_BUSY;
            }
            else
            {
                format_err_reply(reply, sizeof(reply), ERR_INVALID_STATUS);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            format_ok_reply(reply, sizeof(reply), OK_STATUS_SET);
            conn_send(conn, reply, strlen(reply));

            if (me->current_room != NULL && prev != me->status)
            {
                format_notice(reply, sizeof(reply), me->client_name, status_str, me->current_room->room_name);
                room_broadcast(me->current_room, reply, client_fd);
            }
            break;
        }

        case TYPE_WHO:
        {
            if (me->current_room == NULL)
            {
                format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                conn_send(conn, reply, strlen(reply));
                break;
            }

            // Pagination: WHO [offset] [limit]
            int offset = 0;
            int limit = -1;
            if (command.arg1) {
                offset = atoi(command.arg1);
                if (offset < 0) offset = 0;
            }
            if (command.arg2) {
                limit = atoi(command.arg2);
                if (limit < 0) limit = 0;
                if (limit > 1000) limit = 1000;
            }
            int use_pagination = (command.arg1 != NULL || command.arg2 != NULL);

            char who_buf[MAX_LINE_LEN];
            pthread_mutex_lock(&me->current_room->room_lock);
            room_t *cur_room = me->current_room;
            int total = cur_room->member_count;
            if (offset > total) offset = total;
            int remaining = total - offset;
            int count = 0;
            if (limit < 0) count = remaining;
            else count = (limit < remaining) ? limit : remaining;

            if (use_pagination) {
                int off = snprintf(who_buf, sizeof(who_buf), "%s %d %d %d", REPLY_WHO, total, offset, count);
                for (int i = offset; i < offset + count; i++)
                {
                    client_t *m = cur_room->members[i];
                    const char *s = (m->status == PRESENCE_AWAY) ? STATUS_AWAY :
                                    (m->status == PRESENCE_BUSY) ? STATUS_BUSY :
                                                                     STATUS_ONLINE;
                    const char *admin_mark = (cur_room->admin_client == m) ? "*" : "";
                    off += snprintf(who_buf + off, sizeof(who_buf) - off, " %s%s/%s", admin_mark, m->client_name, s);
                    if (off >= (int)sizeof(who_buf) - 32) break;
                }
                pthread_mutex_unlock(&me->current_room->room_lock);
                snprintf(who_buf + off, sizeof(who_buf) - off, "\n");
                conn_send(conn, who_buf, strlen(who_buf));
            } else {
                int off = snprintf(who_buf, sizeof(who_buf), "%s", REPLY_WHO);
                for (int i = 0; i < cur_room->member_count; i++)
                {
                    client_t *m = cur_room->members[i];
                    const char *s = (m->status == PRESENCE_AWAY) ? STATUS_AWAY :
                                    (m->status == PRESENCE_BUSY) ? STATUS_BUSY :
                                                                     STATUS_ONLINE;
                    const char *admin_mark = (cur_room->admin_client == m) ? "*" : "";
                    off += snprintf(who_buf + off, sizeof(who_buf) - off,
                                       " %s%s/%s", admin_mark, m->client_name, s);
                }
                pthread_mutex_unlock(&me->current_room->room_lock);
                snprintf(who_buf + off, sizeof(who_buf) - off, "\n");
                conn_send(conn, who_buf, strlen(who_buf));
            }
            break;
        }
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// handle_client — called by a thread-pool worker each time epoll says the fd
// is readable (or has errored / been closed by the peer).
//
// Flow:
//   1. Lock the conn so two workers can't race on the same buffer.
//   2. Read all currently-available bytes into conn->buf (O_NONBLOCK + EAGAIN
//      tells us we have drained the kernel buffer).
//   3. Extract and process every complete line (\n-terminated).
//   4a. If the client disconnected (recv returned 0 or conn->closing is set):
//       run cleanup, free conn, and return — do NOT re-arm epoll.
//   4b. Otherwise re-arm the fd with EPOLLONESHOT so the next readable event
//       wakes another worker.
// ─────────────────────────────────────────────────────────────────────────────
void handle_client(conn_t *conn)
{
    pthread_mutex_lock(&conn->lock);

    // ── Read available data ───────────────────────────────────────────────────
    int disconnected = conn->closing;

    if (!disconnected)
    {
        ssize_t n;
        while (conn->buf_len < CONN_BUF_SIZE - 1)
        {
            n = conn_recv(conn,
                          conn->buf + conn->buf_len,
                          CONN_BUF_SIZE - 1 - conn->buf_len);
            if (n > 0)
            {
                conn->buf_len += (int)n;
            }
            else if (n == 0)
            {
                // Clean close by peer
                disconnected = 1;
                break;
            }
            else
            {
                // n < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;   // all available data consumed
                // Real error
                disconnected = 1;
                break;
            }
        }
    }

    // ── Process all complete lines ────────────────────────────────────────────
    if (!disconnected)
    {
        // Keep processing lines as long as we can find a newline
        while (1)
        {
            char *nl = (char *)memchr(conn->buf, '\n', (size_t)conn->buf_len);
            if (!nl) break;

            int line_len = (int)(nl - conn->buf);

            // Null-terminate; strip \r if present
            conn->buf[line_len] = '\0';
            if (line_len > 0 && conn->buf[line_len - 1] == '\r')
                conn->buf[line_len - 1] = '\0';

            // Process the line (conn->buf is reused as our scratch space,
            // but process_line only reads it — strtok inside parse_incoming
            // mutates it, which is fine since we compact afterward)
            process_line(conn, conn->buf);

            // Compact the buffer: move everything after the '\n' to the front
            int consumed = line_len + 1;   // +1 for the '\n' itself
            int remaining = conn->buf_len - consumed;
            if (remaining > 0)
                memmove(conn->buf, conn->buf + consumed, (size_t)remaining);
            conn->buf_len = remaining;
        }
    }

    // ── Disconnect or re-arm ──────────────────────────────────────────────────
    if (disconnected)
    {
        pthread_mutex_unlock(&conn->lock);
        disconnect_cleanup(conn);   // closes fd and frees conn
        return;
    }

    pthread_mutex_unlock(&conn->lock);
    epoll_rearm(conn);
}
