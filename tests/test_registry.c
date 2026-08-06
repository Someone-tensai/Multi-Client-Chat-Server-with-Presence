#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/registry.h"

void test_create()
{
    client_t test_client;
    room_err_t err;
    room_t *room = create_room("Test Room", &test_client, &err);
    
    assert(room != NULL);
    assert(err == ROOM_OK);
    assert(strcmp(room->room_name, "Test Room") == 0);
    assert(room->member_count == 1);
    assert(room_count == 1);
    
    room_t *duplicate_room = create_room("Test Room", &test_client, &err);
    assert(duplicate_room == NULL);
    assert(err == ROOM_ERR_ALREADY_EXISTS);

    room_t *long_name_room = create_room("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", &test_client, &err);
    assert(long_name_room == NULL);
    assert(err == ROOM_ERR_INVALID_NAME);

    char room_name_buf[MAX_ROOM_NAME_LEN];
    for(int i = 1; i <= MAX_ROOMS; i++)
    {
        snprintf(room_name_buf, MAX_ROOM_NAME_LEN, "Room %d", i);
        create_room(room_name_buf, &test_client, &err);
    }

    room_t *max_exceeded_room = create_room("Test Room", &test_client, &err);
    assert(max_exceeded_room == NULL);
    assert(err == ROOM_ERR_MAX_ROOMS);

    printf("All Tests Passed! End of Test Create\n");
}

void test_find()
{
    room_err_t err;
    room_t *found_room = find_room("Test Room", &err);
    
    assert(found_room != NULL);
    assert(strcmp(found_room->room_name, "Test Room") == 0);

    room_t *not_found_room = find_room("RANDOM BULLSHIT GO", &err);
    assert(not_found_room == NULL);
    printf("All Tests Passed! End of Test Find\n");
}

int main()
{
    test_create();
    test_find();
    return 0;
}