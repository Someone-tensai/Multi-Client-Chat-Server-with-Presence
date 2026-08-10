#include "../include/server.h"
#include "../include/registry.h"


void handle_client(int client_fd)
{
    client_t *me = NULL;
    char input_buffer[READ_BUFFER_SIZE];
    char reply[MAX_LINE_LEN];
    client_err_t client_err;
    room_err_t room_err;
    ssize_t bytes_received;
    // While Recv is still receiving bytes
    while((bytes_received = recv(client_fd, input_buffer, sizeof(input_buffer)-1, 0)) > 0)
    {
      input_buffer[bytes_received] = '\0';
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
                    format_err_reply(reply, sizeof(reply), ERR_USER_ALREADY_REGISTERED);
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
                                format_ok_reply(reply, sizeof(reply), OK_JOINED);
                                send(client_fd, reply, strlen(reply), 0);
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
                                format_err_reply(reply, sizeof(reply), ERR_MAX_MEMBER_COUNT_REACHED);
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

                                format_notice(reply, sizeof(reply), me->client_name , OK_JOINED , room->room_name);
                                room_broadcast(room_found, reply, me->client_fd);
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
                                format_err_reply(reply, sizeof(reply), ERR_MAX_MEMBER_COUNT_REACHED);
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
                break;
            }

        // Srijal 
        case TYPE_MSG:
            {
                break;
            }    
        // Srijal
        case TYPE_PM:
            {
                break;
            }
        // Srijal
        case TYPE_ROOMS:
            {
                break;
            }
        // Srijal
        case TYPE_WHO:
            {
                break;
            }     
        }
    }
}
}