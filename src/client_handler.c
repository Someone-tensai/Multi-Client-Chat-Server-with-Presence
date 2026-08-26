#include "../include/server.h"
#include "../include/registry.h"
#include "../include/protocol.h"
#include "../include/db.h"
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>
#include <string.h>

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
            // Broadcast using raw send — other clients are plain conn_t* we
            // don't have here, so room_broadcast uses socket_fd directly.
            room_broadcast(last_room, reply, conn->fd);

            room_delete_if_empty(last_room);
        }

        client_err_t cleanup_err;
        delete_client(me, &cleanup_err);
        conn->me = NULL;
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

    if (me == NULL && command.type != TYPE_REGISTER && command.type != TYPE_LOGIN)
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

            conn->me = create_client(client_fd, username, &client_err);
            me = conn->me;

            switch (client_err)
            {
                case CLIENT_OK:
                    format_ok_reply(reply, sizeof(reply), OK_REGISTERED);
                    conn_send(conn, reply, strlen(reply));
                    break;
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

            conn->me = create_client(client_fd, username, &client_err);
            me = conn->me;

            switch (client_err)
            {
                case CLIENT_OK:
                    format_ok_reply(reply, sizeof(reply), OK_LOGGED_IN);
                    conn_send(conn, reply, strlen(reply));
                    break;
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
            send(target->socket_fd, reply, strlen(reply), 0);

            format_notice(reply, sizeof(reply), target_name, OK_KICKED, kicked_from->room_name);
            room_broadcast(kicked_from, reply, target->socket_fd);

            format_ok_reply(reply, sizeof(reply), OK_KICKED);
            conn_send(conn, reply, strlen(reply));

            room_delete_if_empty(kicked_from);
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
            send(target->socket_fd, reply, strlen(reply), 0);

            format_notice(reply, sizeof(reply), target_name, OK_PROMOTED, me->current_room->room_name);
            room_broadcast(me->current_room, reply, -1);
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

            format_msg_reply(reply, sizeof(reply), me->client_name, text);
            room_broadcast(me->current_room, reply, client_fd);

            room_add_history(me->current_room, me->client_name, text);

            format_ok_reply(reply, sizeof(reply), OK_SENT);
            conn_send(conn, reply, strlen(reply));
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
            send(target->socket_fd, reply, strlen(reply), 0);

            format_ok_reply(reply, sizeof(reply), OK_SENT);
            conn_send(conn, reply, strlen(reply));
            break;
        }

        case TYPE_ROOMS:
        {
            char rooms_buf[MAX_LINE_LEN];
            int offset = snprintf(rooms_buf, sizeof(rooms_buf), "%s", REPLY_ROOMS);

            pthread_rwlock_rdlock(&registry_lock);
            for (int i = 0; i < room_count; i++)
                offset += snprintf(rooms_buf + offset, sizeof(rooms_buf) - offset,
                                   " %s", room_list[i]->room_name);
            pthread_rwlock_unlock(&registry_lock);

            snprintf(rooms_buf + offset, sizeof(rooms_buf) - offset, "\n");
            conn_send(conn, rooms_buf, strlen(rooms_buf));
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

            char who_buf[MAX_LINE_LEN];
            int offset = snprintf(who_buf, sizeof(who_buf), "%s", REPLY_WHO);

            pthread_mutex_lock(&me->current_room->room_lock);
            room_t *cur_room = me->current_room;
            for (int i = 0; i < cur_room->member_count; i++)
            {
                client_t *m = cur_room->members[i];
                const char *s = (m->status == PRESENCE_AWAY) ? STATUS_AWAY :
                                (m->status == PRESENCE_BUSY) ? STATUS_BUSY :
                                                                STATUS_ONLINE;
                const char *admin_mark = (cur_room->admin_client == m) ? "*" : "";
                offset += snprintf(who_buf + offset, sizeof(who_buf) - offset,
                                   " %s%s/%s", admin_mark, m->client_name, s);
            }
            pthread_mutex_unlock(&me->current_room->room_lock);

            snprintf(who_buf + offset, sizeof(who_buf) - offset, "\n");
            conn_send(conn, who_buf, strlen(who_buf));
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
