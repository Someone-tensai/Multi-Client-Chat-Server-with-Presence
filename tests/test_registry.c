#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "../include/registry.h"
#include "../include/config.h"

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

static void init_default_registry(void)
{
    registry_destroy();
    server_config_t cfg;
    cfg.max_rooms = CFG_DEFAULT_MAX_ROOMS;
    cfg.max_clients = CFG_DEFAULT_MAX_CLIENTS;
    cfg.max_members = CFG_DEFAULT_MAX_MEMBERS;
    cfg.history_size = CFG_DEFAULT_HISTORY_SIZE;
    cfg.port = CFG_DEFAULT_PORT;
    cfg.thread_pool_size = CFG_DEFAULT_THREAD_POOL_SIZE;
    cfg.rate_bucket_max = CFG_DEFAULT_RATE_BUCKET_MAX;
    cfg.rate_refill_rate = CFG_DEFAULT_RATE_REFILL_RATE;
    cfg.rate_msg_cost = CFG_DEFAULT_RATE_MSG_COST;
    cfg.pool_shrink_idle_sec = CFG_DEFAULT_POOL_SHRINK_IDLE;
    cfg.pool_min_threads = CFG_DEFAULT_POOL_MIN_THREADS;
    strncpy(cfg.tls_cert, CFG_DEFAULT_TLS_CERT, sizeof(cfg.tls_cert)-1);
    strncpy(cfg.tls_key, CFG_DEFAULT_TLS_KEY, sizeof(cfg.tls_key)-1);
    cfg.tls_cert[sizeof(cfg.tls_cert)-1]='\0';
    cfg.tls_key[sizeof(cfg.tls_key)-1]='\0';
    int rc = registry_init(&cfg);
    assert(rc==0);
}

static void init_custom_registry(int max_clients, int max_rooms, int max_members, int history_size)
{
    registry_destroy();
    server_config_t cfg;
    cfg.max_rooms = max_rooms;
    cfg.max_clients = max_clients;
    cfg.max_members = max_members;
    cfg.history_size = history_size;
    cfg.port = CFG_DEFAULT_PORT;
    cfg.thread_pool_size = CFG_DEFAULT_THREAD_POOL_SIZE;
    cfg.rate_bucket_max = CFG_DEFAULT_RATE_BUCKET_MAX;
    cfg.rate_refill_rate = CFG_DEFAULT_RATE_REFILL_RATE;
    cfg.rate_msg_cost = CFG_DEFAULT_RATE_MSG_COST;
    cfg.pool_shrink_idle_sec = CFG_DEFAULT_POOL_SHRINK_IDLE;
    cfg.pool_min_threads = CFG_DEFAULT_POOL_MIN_THREADS;
    strncpy(cfg.tls_cert, CFG_DEFAULT_TLS_CERT, sizeof(cfg.tls_cert)-1);
    strncpy(cfg.tls_key, CFG_DEFAULT_TLS_KEY, sizeof(cfg.tls_key)-1);
    cfg.tls_cert[sizeof(cfg.tls_cert)-1]='\0';
    cfg.tls_key[sizeof(cfg.tls_key)-1]='\0';
    int rc = registry_init(&cfg);
    assert(rc==0);
}

void test_create()
{
    init_default_registry();
    reset_registry();

    client_t test_client;
    memset(&test_client, 0, sizeof(test_client));
    room_err_t err;
    room_t *room = create_room("Test Room", &test_client, &err);

    assert(room != NULL);
    assert(err == ROOM_OK);
    assert(strcmp(room->room_name, "Test Room") == 0);
    // create_room does NOT auto-add member; member_count starts at 0
    assert(room->member_count == 0);
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

    // Fill remaining slots (already have 1 room, so capacity -1 more)
    int cap = room_capacity;
    char room_name_buf[MAX_ROOM_NAME_LEN];
    for(int i = 2; i <= cap; i++)
    {
        snprintf(room_name_buf, MAX_ROOM_NAME_LEN, "Room %d", i);
        room_t *r = create_room(room_name_buf, &test_client, &err);
        assert(r != NULL);
        assert(err == ROOM_OK);
    }
    assert(room_count == cap);

    room_t *max_exceeded_room = create_room("One Too Many", &test_client, &err);
    assert(max_exceeded_room == NULL);
    assert(err == ROOM_ERR_MAX_ROOMS);

    printf("All Tests Passed! End of Test Create (cap=%d)\n", cap);
}

void test_find()
{
    init_default_registry();
    reset_registry();

    client_t test_client;
    memset(&test_client, 0, sizeof(test_client));
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
    init_default_registry();
    reset_registry();

    client_t admin, member1, member2;
    memset(&admin, 0, sizeof(admin));
    memset(&member1, 0, sizeof(member1));
    memset(&member2, 0, sizeof(member2));
    room_err_t err;
    room_t *room = create_room("Test Room", &admin, &err);
    assert(room->member_count == 0);

    // Add admin first (create_room no longer auto-adds)
    room_add_member(room, &admin, &err);
    assert(err == ROOM_OK);
    assert(room->member_count == 1);
    assert(room->members[0] == &admin);

    room_add_member(room, &member1, &err);
    assert(err == ROOM_OK);
    assert(room->member_count == 2);
    assert(room->members[1] == &member1);

    room_add_member(NULL, &member1, &err);
    assert(err == ROOM_ERR_NULL);

    room_add_member(room, NULL, &err);
    assert(err == ROOM_ERR_INVALID_CLIENT);

    // Fill to capacity (already have 2, need capacity -2 more)
    int cap = room->member_capacity;
    // allocate filler on heap to avoid stack overflow for large caps
    int need = cap - 2;
    client_t *filler = NULL;
    if (need > 0) filler = calloc((size_t)need, sizeof(client_t));
    for(int i = 0; i < need; i++)
    {
        room_add_member(room, &filler[i], &err);
        assert(err == ROOM_OK);
    }
    assert(room->member_count == cap);
    if (filler) free(filler);

    room_add_member(room, &member2, &err);
    assert(err == ROOM_ERR_MAX_MEMBERS);
    assert(room->member_count == cap);

    printf("All Tests Passed! End of Test Room Add Member (cap=%d)\n", cap);
}

void test_room_remove_member()
{
    init_default_registry();
    reset_registry();

    client_t admin, member1, member2;
    memset(&admin, 0, sizeof(admin));
    memset(&member1, 0, sizeof(member1));
    memset(&member2, 0, sizeof(member2));
    room_err_t err;
    room_t *room = create_room("Test Room", &admin, &err);
    room_add_member(room, &admin, &err);
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
    init_default_registry();
    reset_registry();

    client_t admin;
    memset(&admin, 0, sizeof(admin));
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
    init_default_registry();
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

    int cap = client_capacity;
    char name_buf[MAX_USERNAME_LEN];
    for(int i = 2; i <= cap; i++)
    {
        snprintf(name_buf, MAX_USERNAME_LEN, "User%d", i);
        client_t *c = create_client(i, name_buf, &err);
        assert(c != NULL);
        assert(err == CLIENT_OK);
    }
    assert(client_count == cap);

    client_t *overflow = create_client(999, "OneTooMany", &err);
    assert(overflow == NULL);
    assert(err == CLIENT_ERR_MAX_CLIENTS);

    printf("All Tests Passed! End of Test Create Client (cap=%d)\n", cap);
}

void test_find_client()
{
    init_default_registry();
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
    init_default_registry();
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

// ── New tests for dynamic sizing ────────────────────────────────────────────

void test_dynamic_small_capacities()
{
    printf("Running dynamic small capacity tests (2 each)...\n");
    init_custom_registry(2, 2, 2, 2);

    room_err_t rerr;
    client_err_t cerr;

    // max_clients =2
    client_t *c1 = create_client(10, "U1", &cerr); assert(c1 && cerr==CLIENT_OK);
    client_t *c2 = create_client(11, "U2", &cerr); assert(c2 && cerr==CLIENT_OK);
    client_t *c3 = create_client(12, "U3", &cerr); assert(c3==NULL && cerr==CLIENT_ERR_MAX_CLIENTS);
    assert(client_count==2 && client_capacity==2);
    // cleanup clients for next test but keep registry capacities
    delete_client(c1, &cerr);
    delete_client(c2, &cerr);
    assert(client_count==0);
    // re-create for room tests
    c1 = create_client(10, "Alice", &cerr);
    c2 = create_client(11, "Bob", &cerr);

    // max_rooms =2
    room_t *r1 = create_room("R1", c1, &rerr); assert(r1 && rerr==ROOM_OK);
    room_t *r2 = create_room("R2", c1, &rerr); assert(r2 && rerr==ROOM_OK);
    room_t *r3 = create_room("R3", c1, &rerr); assert(r3==NULL && rerr==ROOM_ERR_MAX_ROOMS);
    assert(room_count==2 && room_capacity==2);

    // max_members =2
    room_add_member(r1, c1, &rerr); assert(rerr==ROOM_OK); c1->current_room = r1;
    room_add_member(r1, c2, &rerr); assert(rerr==ROOM_OK); c2->current_room = r1;
    client_t extra; // not in client_list, just for member test
    memset(&extra, 0, sizeof(extra));
    strncpy(extra.client_name, "Extra", MAX_USERNAME_LEN-1);
    room_add_member(r1, &extra, &rerr); assert(rerr==ROOM_ERR_MAX_MEMBERS);
    assert(r1->member_count==2 && r1->member_capacity==2);

    // delete and recreate
    delete_room(r2, &rerr); assert(rerr==ROOM_OK);
    assert(room_count==1);
    room_t *r4 = create_room("R3", c1, &rerr); assert(r4 && rerr==ROOM_OK);
    assert(room_count==2);

    // cleanup
    delete_room(r1, &rerr);
    delete_room(r4, &rerr);
    delete_client(c1, &cerr);
    delete_client(c2, &cerr);

    printf("Small capacity tests passed.\n");
}

void test_history_wraparound()
{
    printf("Running history wrap-around test (history_size=2)...\n");
    init_custom_registry(10, 10, 10, 2);
    client_t admin;
    memset(&admin, 0, sizeof(admin));
    room_err_t rerr;
    room_t *room = create_room("HistRoom", &admin, &rerr);
    assert(room && rerr==ROOM_OK);
    assert(room->history_capacity==2);
    assert(room->history_count==0);

    room_add_history(room, "Alice", "msg1");
    assert(room->history_count==1);
    room_add_history(room, "Bob", "msg2");
    assert(room->history_count==2);
    // Next message should wrap and total becomes 3, stored still 2
    room_add_history(room, "Carol", "msg3");
    assert(room->history_count==3);
    // Verify circular buffer: oldest should now be msg2, newest msg3
    // history_start should be 1 after one wrap (started at 0, after 3rd insert start=1)
    assert(room->history_start==1);
    // Check contents: index start (1) should be Bob/msg2, index (start+1)%2=0 should be Carol/msg3
    assert(strcmp(room->history[1].sender, "Bob")==0);
    assert(strcmp(room->history[0].sender, "Carol")==0);

    // Add more to test multiple wraps
    room_add_history(room, "Dave", "msg4");
    assert(room->history_count==4);
    assert(room->history_start==0);
    assert(strcmp(room->history[0].sender, "Carol")==0 || strcmp(room->history[0].sender, "Dave")==0);
    // After 4 inserts with cap2, buffer should contain msg3 and msg4 (the last 2)
    // With total 4, start 0, stored 2: indices 0=Carol(msg3), 1=Dave(msg4) OR depending on logic
    // Our implementation after msg4: start was 1, slot=1 (Dave), new start=0, so history[1]=Dave, history[0]=Carol
    assert(strcmp(room->history[0].sender, "Carol")==0);
    assert(strcmp(room->history[1].sender, "Dave")==0);

    delete_room(room, &rerr);
    printf("History wrap-around tests passed.\n");
}

void test_history_zero_capacity()
{
    printf("Running history zero capacity test...\n");
    init_custom_registry(10, 10, 10, 0);
    client_t admin;
    memset(&admin, 0, sizeof(admin));
    room_err_t rerr;
    room_t *room = create_room("ZeroHist", &admin, &rerr);
    assert(room && rerr==ROOM_OK);
    assert(room->history_capacity==0);
    assert(room->history==NULL);
    // Should not crash when adding history
    room_add_history(room, "Alice", "hello");
    assert(room->history_count==0); // no storage
    // Should not crash on send
    room_send_history(room, -1); // invalid fd but should not crash (we pass -1)
    delete_room(room, &rerr);
    printf("History zero capacity test passed.\n");
}

void test_invalid_config_fallback()
{
    printf("Running invalid config fallback test...\n");
    // Pass zero/negative values — should fallback to defaults
    init_custom_registry(0, -5, 0, -1);
    assert(client_capacity==CFG_DEFAULT_MAX_CLIENTS);
    assert(room_capacity==CFG_DEFAULT_MAX_ROOMS);
    // For members/history, zero should fallback? Our code does >0 check for members, >=0 for history
    // So max_members 0 -> fallback to default 16, history -1 -> fallback to 10
    client_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    room_err_t rerr;
    room_t *r = create_room("FallbackTest", &dummy, &rerr);
    assert(r);
    assert(r->member_capacity==CFG_DEFAULT_MAX_MEMBERS);
    assert(r->history_capacity==CFG_DEFAULT_HISTORY_SIZE);
    delete_room(r, &rerr);
    printf("Invalid config fallback test passed.\n");
}

void test_room_deletion_and_recreation()
{
    printf("Running room deletion and recreation test...\n");
    init_custom_registry(10, 10, 10, 3);
    client_t admin;
    memset(&admin, 0, sizeof(admin));
    room_err_t rerr;
    room_t *room = create_room("RecreateRoom", &admin, &rerr);
    assert(room);
    room_add_history(room, "A", "1");
    room_add_history(room, "A", "2");
    delete_room(room, &rerr);
    assert(rerr==ROOM_OK);
    assert(room_count==0);
    // Recreate same name should succeed and have fresh history
    room = create_room("RecreateRoom", &admin, &rerr);
    assert(room && rerr==ROOM_OK);
    assert(room->history_count==0 || room->history_count==0); // may load from DB; if DB persisted, count may be 2
    // But member count should be 0
    assert(room->member_count==0);
    delete_room(room, &rerr);
    printf("Room deletion and recreation test passed.\n");
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

    test_dynamic_small_capacities();
    test_history_wraparound();
    test_history_zero_capacity();
    test_invalid_config_fallback();
    test_room_deletion_and_recreation();

    // Restore default for any subsequent runs
    init_default_registry();
    reset_registry();

    printf("\nAll registry tests passed (including dynamic tests).\n");
    return 0;
}
