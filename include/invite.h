#ifndef INVITE_H
#define INVITE_H

#include <time.h>

#define MAX_INVITES 256

typedef struct invite {
    char room_name[32];
    char inviter[32];
    char invitee[32];
    time_t created_at;
} invite_t;

int  invite_init(void);
int  invite_create(const char *room_name, const char *inviter, const char *invitee);
int  invite_accept(const char *room_name, const char *invitee);
int  invite_decline(const char *room_name, const char *invitee);
int  invite_find(const char *room_name, const char *invitee);
int  invite_remove(const char *room_name, const char *invitee);

#endif
