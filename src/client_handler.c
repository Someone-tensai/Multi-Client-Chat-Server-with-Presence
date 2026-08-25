#include "../include/server.h"
#include "../include/registry.h"
#include "../include/protocol.h"
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Token bucket rate limiter — uses CLOCK_MONOTONIC for nanosecond precision.
// time(NULL) only has 1-second resolution so fast bursts slip through undetected.
// Returns 1 if allowed, 0 if rate limited.
// ─────────────────────────────────────────────────────────────────────────────
static int rate_limit_check(client_t *client)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    // Elapsed seconds as a precise double
    double elapsed = (now.tv_sec  - client->last_refill.tv_sec) +
                     (now.tv_nsec - client->last_refill.tv_nsec) / 1e9;

    // Refill tokens proportional to elapsed time
    client->tokens += elapsed * RATE_REFILL_RATE;
    if (client->tokens > RATE_BUCKET_MAX)
        client->tokens = RATE_BUCKET_MAX;
    client->last_refill = now;

    if (client->tokens < RATE_MSG_COST)
        return 0;   // bucket empty — reject

    client->tokens -= RATE_MSG_COST;
    return 1;       // allowed
}
void handle_client(int client_fd)
{
    client_t *me = NULL;
    char input_buffer[READ_BUFFER_SIZE];
    char reply[MAX_LINE_LEN];
    client_err_t client_err;
    room_err_t room_err;
    room_t *already_in;
    ssize_t bytes_received;
    // While Recv is still receiving bytes
    while((bytes_received = recv(client_fd, input_buffer, sizeof(input_buffer)-1, 0)) > 0)
    {
      input_buffer[bytes_received] = '\0';
      // Strip trailing \n and \r so commands like "WHO\n" match correctly
      int len = strlen(input_buffer);
      while(len > 0 && (input_buffer[len-1] == '\n' || input_buffer[len-1] == '\r'))
          input_buffer[--len] = '\0';

      cmd command = parse_incoming_command_server(input_buffer);

      if(me == NULL && command.type != TYPE_REGISTER)
      {
        format_err_reply(reply, sizeof(reply), ERR_NOT_REGISTERED);
        send(client_fd, reply, strlen(reply), 0);
      }
      
      else {
      switch(command.type)
      {
        case TYPE_INVALID:
            {   
                format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                send(client_fd, reply, strlen(reply), 0);
                break;
            }

        case TYPE_REGISTER:
            {
                // If Already Registered
                if(me != NULL) 
                {
                    format_err_reply(reply, sizeof(reply), ERR_ALREADY_REGISTERED);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Create Client
                char* username = command.arg1;
                if(username == NULL) 
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }
                me = create_client(client_fd, username, &client_err);

                switch(client_err)
                {
                    case CLIENT_OK:
                        format_ok_reply(reply, sizeof(reply), OK_REGISTERED);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    case CLIENT_ERR_ALLOC_FAILED:
                        format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                        send(client_fd, reply, strlen(reply), 0);
                        break;

                    case CLIENT_ERR_ALREADY_EXISTS:
                        format_err_reply(reply, sizeof(reply), ERR_USERNAME_TAKEN);
                        send(client_fd, reply, strlen(reply), 0);
                        break;

                    case CLIENT_ERR_INVALID_NAME:
                        format_err_reply(reply, sizeof(reply), ERR_INVALID_USERNAME);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    case CLIENT_ERR_MAX_CLIENTS:
                        format_err_reply(reply, sizeof(reply), ERR_MAX_CLIENT_COUNT_REACHED);
                        send(client_fd, reply, strlen(reply), 0);
                        break;

                    default:
                        format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                }
                break;
            }

        case TYPE_CREATE:
            {
                char *room_name = command.arg1;
                if(room_name == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                room_t *new_room = create_room(room_name, me, &room_err);
        
                switch(room_err)
                {
                    case ROOM_OK:
                        
                        room_add_member(new_room, me, &room_err);
                        switch(room_err)
                        {
                            case ROOM_OK:
                                me->current_room = new_room;
                                format_ok_reply(reply, sizeof(reply), OK_CREATED);
                                send(client_fd, reply, strlen(reply), 0);
                                // No history yet in a brand-new room
                                break;

                            case ROOM_ERR_NULL:
                                format_err_reply(reply, sizeof(reply), ERR_ROOM_NULL);
                                send(client_fd, reply, strlen(reply), 0);
                                break;
                            
                            case ROOM_ERR_INVALID_CLIENT:
                                format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                                send(client_fd, reply, strlen(reply), 0);
                                break;

                            case ROOM_ERR_MAX_MEMBERS:
                                format_err_reply(reply, sizeof(reply), ERR_ROOM_MAX_MEMBER_COUNT_REACHED);
                                send(client_fd, reply, strlen(reply), 0);
                                break;

                            default:
                                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                                send(client_fd, reply, strlen(reply), 0);
                                break;
                        }
                        break;

                    case ROOM_ERR_ALLOC_FAILED:
                        format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    case ROOM_ERR_ALREADY_EXISTS:
                        format_err_reply(reply, sizeof(reply), ERR_ROOM_ALREADY_EXISTS);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    case ROOM_ERR_INVALID_CLIENT:
                        format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    case ROOM_ERR_INVALID_NAME:
                        format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    case ROOM_ERR_MAX_ROOMS:
                        format_err_reply(reply, sizeof(reply), ERR_MAX_ROOM_COUNT_REACHED);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                    
                    default:
                        format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                }
                break;
            }

        case TYPE_JOIN:
            {
                char *room_name = command.arg1;
                if(room_name == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                if(me->current_room != NULL)
                {
                    already_in = me -> current_room;
                    room_remove_member(already_in, me, &room_err);
                    switch(room_err)
                    {
                        case ROOM_OK:
                            me->current_room = NULL;
                            break;

                        case ROOM_ERR_NULL:
                            format_err_reply(reply, sizeof(reply), ERR_ROOM_NULL);
                            send(client_fd, reply, strlen(reply), 0);
                            break;

                        case ROOM_ERR_INVALID_CLIENT:
                            format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                            send(client_fd, reply, strlen(reply), 0);
                            break;

                        case ROOM_ERR_CLIENT_NOT_FOUND:
                            format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                            send(client_fd, reply, strlen(reply), 0);
                            break;

                        default:
                            format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                            send(client_fd, reply, strlen(reply), 0);
                            break;
                    }
                }
                room_t *room_found = find_room(room_name, &room_err);
                switch(room_err)
                {
                    case ROOM_OK:
                        room_add_member(room_found, me, &room_err);
                        switch(room_err)
                        {
                            case ROOM_OK:
                                me->current_room = room_found;
                                format_ok_reply(reply, sizeof(reply), OK_JOINED);
                                send(client_fd, reply, strlen(reply), 0);

                                // Replay message history for the joining client
                                room_send_history(room_found, client_fd);

                                format_notice(reply, sizeof(reply), me->client_name , OK_JOINED , room_found->room_name);
                                room_broadcast(room_found, reply, me->socket_fd);
                                break;

                            case ROOM_ERR_NULL:
                                format_err_reply(reply, sizeof(reply), ERR_ROOM_NULL);
                                send(client_fd, reply, strlen(reply), 0);
                                break;
                            
                            case ROOM_ERR_INVALID_CLIENT:
                                format_err_reply(reply, sizeof(reply), ERR_INVALID_CLIENT);
                                send(client_fd, reply, strlen(reply), 0);
                                break;

                            case ROOM_ERR_MAX_MEMBERS:
                                format_err_reply(reply, sizeof(reply), ERR_ROOM_MAX_MEMBER_COUNT_REACHED);
                                send(client_fd, reply, strlen(reply), 0);
                                break;
                            
                            case ROOM_ERR_ALREADY_IN_A_ROOM:
                                format_err_reply(reply, sizeof(reply), ERR_ALREADY_IN_A_ROOM);
                                send(client_fd, reply, strlen(reply), 0);
                                break;

                            default:
                                format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                                send(client_fd, reply, strlen(reply), 0);
                                break;
                        }
                        break;
                        

                    case ROOM_ERR_NOT_FOUND:
                        format_err_reply(reply, sizeof(reply), ERR_ROOM_NOT_FOUND);
                        send(client_fd, reply, strlen(reply), 0);
                        break;

                    case ROOM_ERR_INVALID_NAME:
                        format_err_reply(reply, sizeof(reply), ERR_INVALID_ROOM_NAME);
                        send(client_fd, reply, strlen(reply), 0);
                        break;

                    default:
                        format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                        send(client_fd, reply, strlen(reply), 0);
                        break;
                }
            }    
            break;

        case TYPE_LEAVE:
            {
                // Must be in a room to leave
                if(me->current_room == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                room_t *leaving_room = me->current_room;

                // Remove client from the room
                room_remove_member(leaving_room, me, &room_err);
                if(room_err != ROOM_OK)
                {
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                me->current_room = NULL;

                // Tell the client they left
                format_ok_reply(reply, sizeof(reply), OK_LEFT);
                send(client_fd, reply, strlen(reply), 0);

                // Notify everyone still in the room
                format_notice(reply, sizeof(reply), me->client_name, OK_LEFT, leaving_room->room_name);
                room_broadcast(leaving_room, reply, client_fd);

                // Clean up the room if it is now empty
                room_delete_if_empty(leaving_room);
                break;
            }

        case TYPE_KICK:
            {
                // Must be in a room
                if(me->current_room == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Must be the admin
                if(me->current_room->admin_client != me)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_ADMIN);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                char *target_name = command.arg1;
                if(target_name == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Cannot kick yourself
                if(strcmp(target_name, me->client_name) == 0)
                {
                    format_err_reply(reply, sizeof(reply), ERR_CANNOT_KICK_SELF);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Find target in the global registry
                client_err_t kick_err;
                client_t *target = find_client(target_name, &kick_err);
                if(target == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Target must be in the same room
                if(target->current_room != me->current_room)
                {
                    format_err_reply(reply, sizeof(reply), ERR_USER_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                room_t *kicked_from = me->current_room;

                // Remove target from the room
                room_remove_member(kicked_from, target, &room_err);
                if(room_err != ROOM_OK)
                {
                    format_err_reply(reply, sizeof(reply), ERR_SERVER_ERROR);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }
                target->current_room = NULL;

                // Tell the kicked user what happened
                format_err_reply(reply, sizeof(reply), OK_KICKED);
                send(target->socket_fd, reply, strlen(reply), 0);

                // Notify the whole room
                format_notice(reply, sizeof(reply), target_name, OK_KICKED, kicked_from->room_name);
                room_broadcast(kicked_from, reply, target->socket_fd);

                // Confirm to admin
                format_ok_reply(reply, sizeof(reply), OK_KICKED);
                send(client_fd, reply, strlen(reply), 0);

                // Clean up the room if it is now empty
                room_delete_if_empty(kicked_from);
                break;
            }

        case TYPE_PROMOTE:
            {
                // Must be in a room
                if(me->current_room == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Must be the current admin
                if(me->current_room->admin_client != me)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_ADMIN);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                char *target_name = command.arg1;
                if(target_name == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Find target
                client_err_t promo_err;
                client_t *target = find_client(target_name, &promo_err);
                if(target == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Target must be in the same room
                if(target->current_room != me->current_room)
                {
                    format_err_reply(reply, sizeof(reply), ERR_USER_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Transfer admin
                me->current_room->admin_client = target;

                // Tell the new admin
                format_ok_reply(reply, sizeof(reply), OK_PROMOTED);
                send(target->socket_fd, reply, strlen(reply), 0);

                // Notify the whole room
                format_notice(reply, sizeof(reply), target_name, OK_PROMOTED, me->current_room->room_name);
                room_broadcast(me->current_room, reply, -1);  // -1 = send to everyone incl. old admin

                break;
            }

        case TYPE_MSG:
            {
                // Must be in a room to send a message
                if(me->current_room == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Rate limit check
                if(!rate_limit_check(me))
                {
                    format_err_reply(reply, sizeof(reply), ERR_RATE_LIMITED);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                char *text = command.arg1;
                if(text == NULL || strlen(text) == 0)
                {
                    format_err_reply(reply, sizeof(reply), ERR_EMPTY_MESSAGE);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Broadcast the message to everyone else in the room
                format_msg_reply(reply, sizeof(reply), me->client_name, text);
                room_broadcast(me->current_room, reply, client_fd);

                // Save to room history so late joiners can catch up
                room_add_history(me->current_room, me->client_name, text);

                // Confirm to sender
                format_ok_reply(reply, sizeof(reply), OK_SENT);
                send(client_fd, reply, strlen(reply), 0);
                break;
            }

        case TYPE_PM:
            {
                char *target_name = command.arg1;
                char *text        = command.arg2;

                if(target_name == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                if(text == NULL || strlen(text) == 0)
                {
                    format_err_reply(reply, sizeof(reply), ERR_EMPTY_MESSAGE);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Rate limit check
                if(!rate_limit_check(me))
                {
                    format_err_reply(reply, sizeof(reply), ERR_RATE_LIMITED);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Look up the target user
                client_err_t pm_err;
                client_t *target = find_client(target_name, &pm_err);
                if(target == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_UNKNOWN_USER);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Send the private message to the target
                format_pm_reply(reply, sizeof(reply), me->client_name, text);
                send(target->socket_fd, reply, strlen(reply), 0);

                // Confirm to sender
                format_ok_reply(reply, sizeof(reply), OK_SENT);
                send(client_fd, reply, strlen(reply), 0);
                break;
            }

        case TYPE_ROOMS:
            {
                // Build a list of all room names and send it back
                char rooms_buf[MAX_LINE_LEN];
                int offset = snprintf(rooms_buf, sizeof(rooms_buf), "%s", REPLY_ROOMS);

                pthread_rwlock_rdlock(&registry_lock);
                for(int i = 0; i < room_count; i++)
                {
                    offset += snprintf(rooms_buf + offset, sizeof(rooms_buf) - offset,
                                       " %s", room_list[i]->room_name);
                }
                pthread_rwlock_unlock(&registry_lock);

                snprintf(rooms_buf + offset, sizeof(rooms_buf) - offset, "\n");
                send(client_fd, rooms_buf, strlen(rooms_buf), 0);
                break;
            }

        case TYPE_STATUS:
            {
                char *new_status = command.arg1;
                if(new_status == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                presence_status_t prev = me->status;
                const char *status_str;

                if(strcmp(new_status, STATUS_ONLINE) == 0)
                {
                    me->status = PRESENCE_ONLINE;
                    status_str = STATUS_ONLINE;
                }
                else if(strcmp(new_status, STATUS_AWAY) == 0)
                {
                    me->status = PRESENCE_AWAY;
                    status_str = STATUS_AWAY;
                }
                else if(strcmp(new_status, STATUS_BUSY) == 0)
                {
                    me->status = PRESENCE_BUSY;
                    status_str = STATUS_BUSY;
                }
                else
                {
                    format_err_reply(reply, sizeof(reply), ERR_INVALID_STATUS);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Confirm to client
                format_ok_reply(reply, sizeof(reply), OK_STATUS_SET);
                send(client_fd, reply, strlen(reply), 0);

                // Notify room if the status actually changed and client is in one
                if(me->current_room != NULL && prev != me->status)
                {
                    format_notice(reply, sizeof(reply), me->client_name, status_str, me->current_room->room_name);
                    room_broadcast(me->current_room, reply, client_fd);
                }
                break;
            }

        case TYPE_WHO:
            {
                // Must be in a room to list members
                if(me->current_room == NULL)
                {
                    format_err_reply(reply, sizeof(reply), ERR_NOT_IN_ROOM);
                    send(client_fd, reply, strlen(reply), 0);
                    break;
                }

                // Build list: WHO_REPLY <name>/<status> <name>/<status> ...
                char who_buf[MAX_LINE_LEN];
                int offset = snprintf(who_buf, sizeof(who_buf), "%s", REPLY_WHO);

                pthread_mutex_lock(&me->current_room->room_lock);
                room_t *cur_room = me->current_room;
                for(int i = 0; i < cur_room->member_count; i++)
                {
                    client_t *m = cur_room->members[i];
                    const char *s = (m->status == PRESENCE_AWAY)  ? STATUS_AWAY  :
                                    (m->status == PRESENCE_BUSY)  ? STATUS_BUSY  :
                                                                     STATUS_ONLINE;
                    // Mark the admin with a * prefix
                    const char *admin_mark = (cur_room->admin_client == m) ? "*" : "";
                    offset += snprintf(who_buf + offset, sizeof(who_buf) - offset,
                                       " %s%s/%s", admin_mark, m->client_name, s);
                }
                pthread_mutex_unlock(&me->current_room->room_lock);

                snprintf(who_buf + offset, sizeof(who_buf) - offset, "\n");
                send(client_fd, who_buf, strlen(who_buf), 0);
                break;
            }
        }
    }
    }

    // Cleanup after disconnection
    if(me != NULL)
    {
        // If still in a room, remove and notify others
        if(me->current_room != NULL)
        {
            room_t *last_room = me->current_room;
            room_remove_member(last_room, me, &room_err);
            me->current_room = NULL;

            // Notify remaining members
            format_notice(reply, sizeof(reply), me->client_name, OK_LEFT, last_room->room_name);
            room_broadcast(last_room, reply, client_fd);

            // Clean up the room if it is now empty
            room_delete_if_empty(last_room);
        }

        // Remove client from the global registry
        client_err_t cleanup_err;
        delete_client(me, &cleanup_err);
    }

    // Close the socket
    close(client_fd);
}