#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/registry.h"

void reset_registry()
{
    while(room_count > 0)
    {
        room_err_t err;
        delete_room(room_list[0], &err);
    }
    while(client_count > 0)
    {
        client_err_t err;
        delete_client(client_list[0], &err);
    }
}

void test_create()
{
    reset_registry();

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

    room_t *empty_name_room = create_room("", &test_client, &err);
    assert(empty_name_room == NULL);
    assert(err == ROOM_ERR_INVALID_NAME);

    // Fill remaining slots (already have 1 room, so MAX_ROOMS - 1 more)
    char room_name_buf[MAX_ROOM_NAME_LEN];
    for(int i = 2; i <= MAX_ROOMS; i++)
    {
        snprintf(room_name_buf, MAX_ROOM_NAME_LEN, "Room %d", i);
        room_t *r = create_room(room_name_buf, &test_client, &err);
        assert(r != NULL);
        assert(err == ROOM_OK);
    }
    assert(room_count == MAX_ROOMS);

    room_t *max_exceeded_room = create_room("One Too Many", &test_client, &err);
    assert(max_exceeded_room == NULL);
    assert(err == ROOM_ERR_MAX_ROOMS);

    printf("All Tests Passed! End of Test Create\n");
}

void test_find()
{
    reset_registry();

    client_t test_client;
    room_err_t err;
    create_room("Test Room", &test_client, &err);

    room_t *found_room = find_room("Test Room", &err);
    assert(found_room != NULL);
    assert(strcmp(found_room->room_name, "Test Room") == 0);
    assert(err == ROOM_OK);

    room_t *not_found_room = find_room("RANDOM BULLSHIT GO", &err);
    assert(not_found_room == NULL);
    assert(err == ROOM_ERR_NOT_FOUND);

    printf("All Tests Passed! End of Test Find\n");
}

void test_room_add_member()
{
    reset_registry();

    client_t admin, member1, member2;
    room_err_t err;
    room_t *room = create_room("Test Room", &admin, &err);
    assert(room->member_count == 1);

    room_add_member(room, &member1, &err);
    assert(err == ROOM_OK);
    assert(room->member_count == 2);
    assert(room->members[1] == &member1);

    room_add_member(NULL, &member1, &err);
    assert(err == ROOM_ERR_NULL);

    room_add_member(room, NULL, &err);
    assert(err == ROOM_ERR_INVALID_CLIENT);

    // Fill to capacity (already have 2, need MAX_MEMBERS - 2 more)
    client_t filler_clients[MAX_MEMBERS];
    for(int i = 0; i < MAX_MEMBERS - 2; i++)
    {
        room_add_member(room, &filler_clients[i], &err);
        assert(err == ROOM_OK);
    }
    assert(room->member_count == MAX_MEMBERS);

    room_add_member(room, &member2, &err);
    assert(err == ROOM_ERR_MAX_MEMBERS);
    assert(room->member_count == MAX_MEMBERS);

    printf("All Tests Passed! End of Test Room Add Member\n");
}

void test_room_remove_member()
{
    reset_registry();

    client_t admin, member1, member2;
    room_err_t err;
    room_t *room = create_room("Test Room", &admin, &err);
    room_add_member(room, &member1, &err);
    room_add_member(room, &member2, &err);
    assert(room->member_count == 3);

    // Remove from the middle, verify shift happened correctly
    room_remove_member(room, &member1, &err);
    assert(err == ROOM_OK);
    assert(room->member_count == 2);
    assert(room->members[0] == &admin);
    assert(room->members[1] == &member2);

    // Client not in room
    room_remove_member(room, &member1, &err);
    assert(err == ROOM_ERR_CLIENT_NOT_FOUND);

    room_remove_member(NULL, &member2, &err);
    assert(err == ROOM_ERR_NULL);

    room_remove_member(room, NULL, &err);
    assert(err == ROOM_ERR_INVALID_CLIENT);

    printf("All Tests Passed! End of Test Room Remove Member\n");
}

void test_delete_room()
{
    reset_registry();

    client_t admin;
    room_err_t err;
    room_t *room_a = create_room("Room A", &admin, &err);
    room_t *room_b = create_room("Room B", &admin, &err);
    room_t *room_c = create_room("Room C", &admin, &err);
    assert(room_count == 3);

    // Delete from the middle, verify shift + lookup consistency
    delete_room(room_b, &err);
    assert(err == ROOM_OK);
    assert(room_count == 2);
    assert(find_room("Room B", &err) == NULL);
    assert(find_room("Room A", &err) == room_a);
    assert(find_room("Room C", &err) == room_c);

    delete_room(NULL, &err);
    assert(err == ROOM_ERR_NULL);

    // Room already deleted / not in list
    delete_room(room_b, &err);
    assert(err == ROOM_ERR_NOT_FOUND);

    printf("All Tests Passed! End of Test Delete Room\n");
}

void test_create_client()
{
    reset_registry();

    client_err_t err;
    client_t *client = create_client(5, "ABC", &err);

    assert(client != NULL);
    assert(err == CLIENT_OK);
    assert(strcmp(client->client_name, "ABC") == 0);
    assert(client->socket_fd == 5);
    assert(client->current_room == NULL);
    assert(client_count == 1);

    client_t *dup = create_client(6, "ABC", &err);
    assert(dup == NULL);
    assert(err == CLIENT_ERR_ALREADY_EXISTS);

    client_t *empty_name = create_client(7, "", &err);
    assert(empty_name == NULL);
    assert(err == CLIENT_ERR_INVALID_NAME);

    char name_buf[MAX_USERNAME_LEN];
    for(int i = 2; i <= MAX_CLIENTS; i++)
    {
        snprintf(name_buf, MAX_USERNAME_LEN, "User%d", i);
        client_t *c = create_client(i, name_buf, &err);
        assert(c != NULL);
        assert(err == CLIENT_OK);
    }
    assert(client_count == MAX_CLIENTS);

    client_t *overflow = create_client(999, "OneTooMany", &err);
    assert(overflow == NULL);
    assert(err == CLIENT_ERR_MAX_CLIENTS);

    printf("All Tests Passed! End of Test Create Client\n");
}

void test_find_client()
{
    reset_registry();

    client_err_t err;
    create_client(5, "ABC", &err);

    client_t *found = find_client("ABC", &err);
    assert(found != NULL);
    assert(strcmp(found->client_name, "ABC") == 0);
    assert(err == CLIENT_OK);

    client_t *not_found = find_client("Nobody", &err);
    assert(not_found == NULL);
    assert(err == CLIENT_ERR_NOT_FOUND);

    printf("All Tests Passed! End of Test Find Client\n");
}

void test_delete_client()
{
    reset_registry();

    client_err_t err;
    client_t *a = create_client(1, "Alice", &err);
    client_t *b = create_client(2, "Bob", &err);
    client_t *c = create_client(3, "Carol", &err);
    assert(client_count == 3);

    delete_client(b, &err);
    assert(err == CLIENT_OK);
    assert(client_count == 2);
    assert(find_client("Bob", &err) == NULL);
    assert(find_client("Alice", &err) == a);
    assert(find_client("Carol", &err) == c);

    delete_client(NULL, &err);
    assert(err == CLIENT_ERR_NULL);

    delete_client(b, &err);
    assert(err == CLIENT_ERR_NOT_FOUND);

    printf("All Tests Passed! End of Test Delete Client\n");
}

int main()
{
    test_create();
    test_find();
    test_room_add_member();
    test_room_remove_member();
    test_delete_room();
    test_create_client();
    test_find_client();
    test_delete_client();

    printf("\nAll registry tests passed.\n");
    return 0;
}