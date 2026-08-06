#include "../include/registry.h"
#include "../include/protocol.h"
#include <string.h>
#include <malloc.h>
#include <sys/socket.h>

// Defining the Extern Variables
pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
room_t * room_list[MAX_ROOMS];
int room_count = 0;

client_t * client_list[MAX_CLIENTS];
int client_count = 0;
// Implementation of Registry Functions

// Room Related Functions

// Find a Room (Unlocked to be wrapped by functions with mutex locks)
room_t *find_room_unlocked(const char* room_name, room_err_t *err)
{
    // Loop Through All Rooms and Compare their Names
    for(int i = 0; i < room_count ; i++)
    {
        if(strcmp(room_list[i]->room_name, room_name) == 0)
        {
            return room_list[i];
        }
    }
    *err = ROOM_ERR_NOT_FOUND;
    return NULL;
}

// Create a New Room
room_t* create_room(const char *room_name, client_t *creator_client, room_err_t *err)
{

    // If length of room name is more than allowed
    size_t len = strlen(room_name);
    if(len == 0 || len >= MAX_ROOM_NAME_LEN)
    {
        *err = ROOM_ERR_INVALID_NAME;
        return NULL;
    }

    // Thread Safe Lock
    pthread_mutex_lock(&registry_lock);

    // If Max number of rooms have already been created
    if(room_count >= MAX_ROOMS)
    {
        pthread_mutex_unlock(&registry_lock);
        *err = ROOM_ERR_MAX_ROOMS;
        return NULL;
    }

    // See if a room by the name already exists
    room_t* already_existing_room = find_room_unlocked(room_name, err);
    if(already_existing_room != NULL) 
    {
        pthread_mutex_unlock(&registry_lock);
        *err = ROOM_ERR_ALREADY_EXISTS;
        return NULL;
    }

    // Create the room and initialise its members
    room_t* new_room = malloc(sizeof(room_t));

    if(!new_room) 
    {
        pthread_mutex_unlock(&registry_lock);
        *err = ROOM_ERR_ALLOC_FAILED;
        return NULL;
    }

    strncpy(new_room->room_name, room_name, MAX_ROOM_NAME_LEN);
    new_room->room_name[MAX_ROOM_NAME_LEN-1] = '\0';
    new_room->admin_client = creator_client;
    new_room->member_count = 1;
    new_room->members[0] = creator_client;

    // Add the room to the global room list
    room_list[room_count++] = new_room;

    pthread_mutex_unlock(&registry_lock);
    *err = ROOM_OK;
    return new_room;
}

// Find Room (With a Lock for Thread Safe Finding)
room_t *find_room(const char *room_name, room_err_t *err)
{
    pthread_mutex_lock(&registry_lock);
    room_t *found_room = find_room_unlocked(room_name, err);
    pthread_mutex_unlock(&registry_lock);
    return found_room;
}

// When a New Member joins a Room
void room_add_member(room_t *room, client_t *client, room_err_t *err)
{

    // Invalid or Null Room
    if(room == NULL)
    {
        *err = ROOM_ERR_NULL;
        return;
    }

      // If Client is Null
    if(client == NULL)
    {
        *err = ROOM_ERR_INVALID_CLIENT;
        return;
    }

    pthread_mutex_lock(&registry_lock);
    // Check if Max Members Reached
    if(room->member_count >= MAX_MEMBERS)
    {
        *err = ROOM_ERR_MAX_MEMBERS;
        pthread_mutex_unlock(&registry_lock);
        return;
    }

  
    // Increase Member Count
    // Assign Member to the room
    room->member_count += 1;
    room->members[room->member_count-1] = client; 
    pthread_mutex_unlock(&registry_lock);
    *err = ROOM_OK;
    return;
}

void shift_array_room(room_t *room, int start_index, int end_index)
{
    for(int i = start_index ; i < end_index; i++)
    {
        room->members[i] = room->members[i+1];
    }
}
// When a Member leaves a Room
void room_remove_member(room_t *room, client_t *client, room_err_t *err)
{
    // Invalid or Null Room
    if(room == NULL)
    {
        *err = ROOM_ERR_NULL;
        return;
    }

      // If Client is Null
    if(client == NULL)
    {
        *err = ROOM_ERR_INVALID_CLIENT;
        return;
    }

    pthread_mutex_lock(&registry_lock);
    // Check if Max Members Reached
    if(room->member_count <= 0)
    {
        *err = ROOM_ERR_NULL;
        pthread_mutex_unlock(&registry_lock);
        return;
    }
    // Find Client's Index (Not same as their id as that is in the global client list not in room members)
    int client_index = -1;
    for(int i = 0 ; i < room->member_count; i++)
    {
        if(room->members[i] == client)
        {
            client_index = i;
            break;
        }
    }

    // If Client Not in Room
    if(client_index == -1)
    {
        *err = ROOM_ERR_CLIENT_NOT_FOUND;
        pthread_mutex_unlock(&registry_lock);
        return;
    }
    // If Removing from end , this is enough
    room->member_count -= 1;

    // If Removing from anywhere else
    if(client_index != room->member_count) 
    {
        // Shift them one step back from where they are deleted
        shift_array_room(room, client_index , room->member_count);
    }
    
    pthread_mutex_unlock(&registry_lock);
    *err = ROOM_OK;
    return;
}

// Message Brodcast to all clients (The Chat essentially)
void room_broadcast(room_t *room, const char *msg, int exclude_fd)
{
    pthread_mutex_lock(&registry_lock);
    
    for(int i = 0; i < room->member_count; i++)
    {
        // Get Individual fds
        int fd = room->members[i]->socket_fd;
        // Exclude the client who sent the message
        if(fd == exclude_fd) continue;
        send(fd, msg, strlen(msg), 0);
    }
    pthread_mutex_unlock(&registry_lock);
}   

void delete_room(room_t *room, room_err_t *err)
{

}

// Client Related Functions
client_t *create_client(int socket_fd, const char* client_name, client_err_t *err) 
{
    // If length of room name is more than allowed
    size_t len = strlen(client_name);
    if(len == 0 || len >= MAX_USERNAME_LEN)
    {
        *err = CLIENT_ERR_INVALID_NAME;
        return NULL;
    }

    // Thread Safe Lock
    pthread_mutex_lock(&registry_lock);

    // If Max number of clients have already been created
    if(client_count >= MAX_CLIENTS)
    {
        pthread_mutex_unlock(&registry_lock);
        *err = CLIENT_ERR_MAX_CLIENTS;
        return NULL;
    }

    // See if a client by the name already exists
    client_t* already_existing_client = find_client_unlocked(client_name, err);
    if(already_existing_client != NULL) 
    {
        pthread_mutex_unlock(&registry_lock);
        *err = CLIENT_ERR_ALREADY_EXISTS;
        return NULL;
    }

    // Create the client and initialise its members
    client_t* new_client = malloc(sizeof(client_t));

    if(!new_client) 
    {
        pthread_mutex_unlock(&registry_lock);
        *err = CLIENT_ERR_ALLOC_FAILED;
        return NULL;
    }

    strncpy(new_client->client_name, client_name, MAX_USERNAME_LEN);
    new_client->client_name[MAX_USERNAME_LEN-1] = '\0';
    new_client->socket_fd = socket_fd;    
    new_client->client_id = client_count;

    // Add the client to the global client list
    client_list[client_count++] = new_client;

    pthread_mutex_unlock(&registry_lock);
    *err = CLIENT_OK;
    return new_client;

}

client_t* find_client_unlocked(const char *username, client_err_t *err)
{
    // Loop Through All Made Clients and Compare their IDs
    for(int i = 0; i < client_count ; i++)
    {
        if(strcmp(client_list[i]->client_name, username) == 0)
        {
            return client_list[i];
        }
    }
    *err = CLIENT_ERR_NOT_FOUND;
    return NULL;

}

client_t* find_client(const char *username, client_err_t *err)
{
    pthread_mutex_lock(&registry_lock);
    client_t *found_client = find_client_unlocked(username, err);
    pthread_mutex_unlock(&registry_lock);
    return found_client;

}

void delete_client(client_t *client, client_err_t *err)
{

}