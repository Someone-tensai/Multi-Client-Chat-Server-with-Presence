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
            format_err_reply(reply, sizeof(reply), ERR_INVALID_COMMAND);
            send(client_fd, reply, strlen(reply), 0);
            break;
        
        case TYPE_REGISTER:
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
                    format_err_reply(reply, sizeof(reply), ERR_ALREADY_REGISTERED);
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
        
        case TYPE_CREATE:
            break;
        
        case TYPE_JOIN:
            break;

        case TYPE_LEAVE:
            break;

        case TYPE_MSG:
            break;
        
        case TYPE_PM:
            break;
        
        case TYPE_ROOMS:
            break;

        case TYPE_WHO:
            break;
      }
    }
}
}