#ifndef BLOCK_H
#define BLOCK_H

#define MAX_BLOCKS 256

typedef struct block_entry {
    char blocker[32];
    char blocked[32];
} block_entry_t;

int  block_init(void);
int  block_add(const char *blocker, const char *blocked);
int  block_remove(const char *blocker, const char *blocked);
int  block_check(const char *blocker, const char *blocked);
int  block_list(const char *blocker, char out[][32], int max_out);
int  block_count(const char *blocker);

int  mute_add(const char *room_name, const char *muted_by, const char *muted_user);
int  mute_remove(const char *room_name, const char *muted_user);
int  mute_check(const char *room_name, const char *muted_user);

int  ban_add(const char *room_name, const char *banned_by, const char *banned_user);
int  ban_remove(const char *room_name, const char *banned_user);
int  ban_check(const char *room_name, const char *banned_user);

#endif
