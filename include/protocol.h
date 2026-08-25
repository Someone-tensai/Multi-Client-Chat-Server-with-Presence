// To ensure no circular includes
#ifndef PROTOCOL_H
#define PROTOCOL_H

// ENUM to recognise command type
typedef enum {
    TYPE_REGISTER,
    TYPE_CREATE,
    TYPE_JOIN,
    TYPE_MSG,
    TYPE_PM,
    TYPE_WHO,
    TYPE_ROOMS,
    TYPE_LEAVE,
    TYPE_INVALID   // unknown/malformed command
} cmd_type;

// Struct Used for Parsing and stuff

typedef struct cmd
{
  cmd_type type;
  char* arg1;
  char* arg2;
} cmd;

// Maximum lengths of messages, usernames etc
#define MAX_LINE_LEN 512
#define MAX_USERNAME_LEN 32
#define MAX_ROOM_NAME_LEN 32
#define MAX_TEXT_LEN 400

// Defining all words to use in communication

// Example Interaction

/*
  Client commands [CMD_REGISTER <username>]
    Server Sees "REGISTER <username>"
    Server tries to register user
    If Successful
        Replies [REPLY_OK OK_REGISTERED]
        Client Sees "OK REGISTERED"
    
    If Error
        Replies [REPLY_ERR ERR_USERNAME_TAKEN]
        Client Sees "ERR USERNAME_TAKEN"
*/

// Client -> Server
#define CMD_REGISTER "REGISTER"
#define CMD_CREATE "CREATE"
#define CMD_JOIN "JOIN"
#define CMD_MSG "MSG"
#define CMD_PM "PM"
#define CMD_WHO "WHO"
#define CMD_ROOMS "ROOMS"
#define CMD_LEAVE "LEAVE"
#define CMD_INVALID "INVALID"
// Server -> Client 
#define REPLY_OK "OK"
#define REPLY_ERR "ERR"
#define REPLY_NOTICE "NOTICE"
#define REPLY_MSG "MSG"
#define REPLY_PM_FROM "PM_FROM"
#define REPLY_WHO "WHO_REPLY"
#define REPLY_ROOMS "ROOMS_REPLY"

// If the command succeded
#define OK_REGISTERED "REGISTERED"
#define OK_CREATED "CREATED"
#define OK_JOINED "JOINED"
#define OK_LEFT "LEFT"
#define OK_SENT "SENT"

// If there was an error

#define ERR_SERVER_ERROR "SERVER_ERROR"
#define ERR_INVALID_COMMAND "INVALID_COMMAND"

#define ERR_USERNAME_TAKEN "USERNAME_TAKEN"
#define ERR_INVALID_USERNAME "INVALID_USERNAME"
#define ERR_NOT_REGISTERED "NOT_REGISTERED"
#define ERR_ALREADY_REGISTERED "ALREADY_REGISTERED"
#define ERR_UNKNOWN_USER "UNKNOWN_USER"
#define ERR_MAX_CLIENT_COUNT_REACHED "MAX_CLIENT_COUNT_REACHED"
#define ERR_INVALID_CLIENT "INVALID_CLIENT"

#define ERR_INVALID_ROOM_NAME "INVALID_ROOM_NAME"
#define ERR_ROOM_NOT_FOUND "ROOM_NOT_FOUND"
#define ERR_ROOM_ALREADY_EXISTS "ROOM_ALREADY_EXISTS"
#define ERR_MAX_ROOM_COUNT_REACHED "MAX_ROOM_COUNT_REACHED"
#define ERR_ROOM_MALLOC_ERROR "ROOM_MALLOC_ERROR"
#define ERR_NOT_IN_ROOM "NOT_IN_ROOM"
#define ERR_ROOM_NULL "ROOM_NULL"
#define ERR_ROOM_MAX_MEMBER_COUNT_REACHED "ROOM_MAX_MEMBERS_REACHED"
#define ERR_ALREADY_IN_A_ROOM "ALREADY_IN_A_ROOM"

#define ERR_EMPTY_MESSAGE "EMPTY_MESSAGE"
#define ERR_LINE_TOO_LONG "LINE_TOO_LONG"

#define ERR_UNKNOWN_COMMAND "UNKNOWN_COMMAND"
#define ERR_MALFORMED "MALFORMED"

// Function Declarations
#include <stddef.h>
cmd parse_incoming_command_server(char *line);
void format_ok_reply(char *out, size_t out_size, const char *status);
void format_err_reply(char *out, size_t out_size, const char *err_code);
void format_notice(char *out, size_t out_size, const char *user, const char *action, const char *room);
void format_msg_reply(char *out, size_t out_size, const char *sender, const char *text);
void format_pm_reply(char *out, size_t out_size, const char *sender, const char *text);

#endif