#ifndef PERMISSION_H
#define PERMISSION_H

typedef enum {
    ROLE_MEMBER = 0,
    ROLE_MODERATOR,
    ROLE_ADMIN,
    ROLE_OWNER
} room_role_t;

typedef struct {
    char username[32];
    room_role_t role;
} room_member_role_t;

int  permission_init(void);
int  permission_set_role(const char *room_name, const char *username, room_role_t role);
int  permission_get_role(const char *room_name, const char *username, room_role_t *out);
int  permission_check(const char *room_name, const char *username, const char *action);
int  permission_remove(const char *room_name, const char *username);

#define ACTION_KICK   "KICK"
#define ACTION_BAN    "BAN"
#define ACTION_MUTE   "MUTE"
#define ACTION_DEMOTE "DEMOTE"
#define ACTION_MSG    "MSG"
#define ACTION_INVITE "INVITE"

#endif
