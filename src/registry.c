#include "registry.h"
#include "protocol.h"

// Implementation of Registry Functions

// Room Related Functions
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
    room_t* already_existing_room = find_room_unlocked(room_name);
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

room_t *find_room_unlocked(const char* room_name, room_err_t *err)
{

}

room_t *find_room(const char *room_name, room_err_t *err)
{

}

void room_add_member(room_t *room, client_t *client, room_err_t *err)
{

}
void room_remove_member(room_t *room, client_t *client, room_err_t *err)
{

}
void room_broadcast(room_t *room, const char *msg, int exclude_fd)
{

}

void delete_room(room_t *room, room_err_t *err)
{

}

// Client Related Functions
client_t *registry_create_client(int socket_fd, const char* client_name, client_err_t *err) 
{

}

client_t* registry_find_client(const char *username, client_err_t *err)
{

}

void delete_client(client_t *client, client_err_t *err)
{

}