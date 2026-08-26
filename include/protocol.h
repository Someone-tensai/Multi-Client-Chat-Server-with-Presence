// To ensure no circular includes
#ifndef PROTOCOL_H
#define PROTOCOL_H

// ENUM to recognise command type
typedef enum {
    TYPE_REGISTER,
    TYPE_LOGIN,
    TYPE_RECONNECT,
    TYPE_CREATE,
    TYPE_JOIN,
    TYPE_MSG,
    TYPE_PM,
    TYPE_WHO,
    TYPE_ROOMS,
    TYPE_LEAVE,
    TYPE_KICK,
    TYPE_PROMOTE,
    TYPE_STATUS,
    TYPE_EDIT,
    TYPE_DELETE,
    TYPE_HISTORY,
    TYPE_LOGOUT,
    TYPE_LOGOUT_ALL,
    TYPE_SESSIONS,
    TYPE_REVOKE_SESSION,
    TYPE_READ,
    TYPE_TYPING,
    TYPE_STOP_TYPING,
    TYPE_BLOCK,
    TYPE_UNBLOCK,
    TYPE_MUTE,
    TYPE_UNMUTE,
    TYPE_BAN,
    TYPE_DEMOTE,
    TYPE_CREATE_PRIVATE,
    TYPE_INVITE,
    TYPE_ACCEPT,
    TYPE_DECLINE,
    TYPE_INVALID
} cmd_type;

// Struct Used for Parsing and stuff

typedef struct cmd
{
  cmd_type type;
  char* arg1;
  char* arg2;
  char* arg3;
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
#define CMD_LOGIN "LOGIN"
#define CMD_RECONNECT "RECONNECT"
#define CMD_CREATE "CREATE"
#define CMD_JOIN "JOIN"
#define CMD_MSG "MSG"
#define CMD_PM "PM"
#define CMD_WHO "WHO"
#define CMD_ROOMS "ROOMS"
#define CMD_LEAVE "LEAVE"
#define CMD_KICK "KICK"
#define CMD_PROMOTE "PROMOTE"
#define CMD_STATUS "STATUS"
#define CMD_EDIT "EDIT"
#define CMD_DELETE "DELETE"
#define CMD_HISTORY "HISTORY"
#define CMD_LOGOUT "LOGOUT"
#define CMD_LOGOUT_ALL "LOGOUT_ALL"
#define CMD_SESSIONS "SESSIONS"
#define CMD_REVOKE_SESSION "REVOKE_SESSION"
#define CMD_READ "READ"
#define CMD_TYPING "TYPING"
#define CMD_STOP_TYPING "STOP_TYPING"
#define CMD_BLOCK "BLOCK"
#define CMD_UNBLOCK "UNBLOCK"
#define CMD_MUTE "MUTE"
#define CMD_UNMUTE "UNMUTE"
#define CMD_BAN "BAN"
#define CMD_DEMOTE "DEMOTE"
#define CMD_CREATE_PRIVATE "CREATE_PRIVATE"
#define CMD_INVITE "INVITE"
#define CMD_ACCEPT "ACCEPT"
#define CMD_DECLINE "DECLINE"
#define CMD_INVALID "INVALID"
// Server -> Client 
#define REPLY_OK "OK"
#define REPLY_ERR "ERR"
#define REPLY_NOTICE "NOTICE"
#define REPLY_MSG "MSG"
#define REPLY_PM_FROM "PM_FROM"
#define REPLY_WHO "WHO_REPLY"
#define REPLY_ROOMS "ROOMS_REPLY"
#define REPLY_HISTORY "HISTORY_REPLY"
#define REPLY_EDITED "MESSAGE_EDITED"
#define REPLY_DELETED "MESSAGE_DELETED"
#define REPLY_SESSIONS "SESSIONS_REPLY"
#define REPLY_READ_RECEIPT "READ_RECEIPT"
#define REPLY_TYPING "TYPING"
#define REPLY_STOP_TYPING "STOP_TYPING"
#define REPLY_LOGOUT "LOGOUT_OK"
#define REPLY_INVITE "INVITE"
#define REPLY_INVITE_DECLINED "INVITE_DECLINED"

// Presence status values (used in STATUS command and WHO reply)
#define STATUS_ONLINE "ONLINE"
#define STATUS_AWAY   "AWAY"
#define STATUS_BUSY   "BUSY"

// If the command succeded
#define OK_REGISTERED "REGISTERED"
#define OK_LOGGED_IN  "LOGGED_IN"
#define OK_RECONNECTED "RECONNECTED"
#define OK_SESSION "SESSION"
#define OK_CREATED "CREATED"
#define OK_JOINED "JOINED"
#define OK_LEFT "LEFT"
#define OK_SENT "SENT"
#define OK_KICKED "KICKED"
#define OK_PROMOTED "PROMOTED"
#define OK_STATUS_SET "STATUS_SET"
#define OK_BLOCKED "BLOCKED"
#define OK_UNBLOCKED "UNBLOCKED"
#define OK_MUTED "MUTED"
#define OK_UNMUTED "UNMUTED"
#define OK_BANNED "BANNED"
#define OK_DEMOTED "DEMOTED"
#define OK_INVITED "INVITED"
#define OK_INVITE_ACCEPTED "INVITE_ACCEPTED"
#define OK_INVITE_DECLINED "INVITE_DECLINED"
#define OK_CREATED_PRIVATE "CREATED_PRIVATE"

// If there was an error

#define ERR_SERVER_ERROR "SERVER_ERROR"
#define ERR_SERVER_SHUTDOWN "SERVER_SHUTDOWN"
#define ERR_INVALID_COMMAND "INVALID_COMMAND"

#define ERR_USERNAME_TAKEN "USERNAME_TAKEN"
#define ERR_WRONG_PASSWORD "WRONG_PASSWORD"
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
#define ERR_NOT_ADMIN "NOT_ADMIN"
#define ERR_CANNOT_KICK_SELF "CANNOT_KICK_SELF"
#define ERR_USER_NOT_IN_ROOM "USER_NOT_IN_ROOM"
#define ERR_INVALID_STATUS "INVALID_STATUS"
#define ERR_RATE_LIMITED "RATE_LIMITED"

#define ERR_EMPTY_MESSAGE "EMPTY_MESSAGE"
#define ERR_LINE_TOO_LONG "LINE_TOO_LONG"

#define ERR_UNKNOWN_COMMAND "UNKNOWN_COMMAND"
#define ERR_MALFORMED "MALFORMED"
#define ERR_INVALID_TOKEN "INVALID_TOKEN"
#define ERR_SESSION_EXPIRED "SESSION_EXPIRED"
#define ERR_MSG_NOT_FOUND "MSG_NOT_FOUND"
#define ERR_NOT_AUTHORIZED "NOT_AUTHORIZED"
#define ERR_MSG_DELETED "MSG_DELETED"
#define ERR_SESSION_NOT_FOUND "SESSION_NOT_FOUND"
#define ERR_CANNOT_REVOKE_OWN "CANNOT_REVOKE_OWN"
#define ERR_USER_BLOCKED "USER_BLOCKED"
#define ERR_USER_NOT_BLOCKED "USER_NOT_BLOCKED"
#define ERR_USER_MUTED "USER_MUTED"
#define ERR_USER_NOT_MUTED "USER_NOT_MUTED"
#define ERR_USER_BANNED "USER_BANNED"
#define ERR_NOT_ROOM_OWNER "NOT_ROOM_OWNER"
#define ERR_CANNOT_DEMOTESELF "CANNOT_DEMOTE_SELF"
#define ERR_INVITE_FAILED "INVITE_FAILED"
#define ERR_ALREADY_INVITED "ALREADY_INVITED"
#define ERR_ROOM_PRIVATE "ROOM_PRIVATE"
#define ERR_NOT_INVITED "NOT_INVITED"
#define ERR_LOGIN_RATE_LIMITED "LOGIN_RATE_LIMITED"
#define ERR_ACCOUNT_LOCKED "ACCOUNT_LOCKED"
#define ERR_INVALID_ROLE "INVALID_ROLE"

// Session token length (hex, 32 bytes = 64 chars)
#define SESSION_TOKEN_LEN 64

// Function Declarations
#include <stddef.h>
cmd parse_incoming_command_server(char *line);
void format_ok_reply(char *out, size_t out_size, const char *status);
void format_ok_session(char *out, size_t out_size, const char *status, const char *token);
void format_err_reply(char *out, size_t out_size, const char *err_code);
void format_notice(char *out, size_t out_size, const char *user, const char *action, const char *room);
void format_msg_reply(char *out, size_t out_size, const char *sender, const char *text);
void format_msg_reply_id(char *out, size_t out_size, long long msg_id, const char *sender, const char *text);
void format_history_reply(char *out, size_t out_size, const char *room, long long cursor, int count);
void format_edited_reply(char *out, size_t out_size, long long msg_id, const char *new_text);
void format_deleted_reply(char *out, size_t out_size, long long msg_id);
void format_pm_reply(char *out, size_t out_size, const char *sender, const char *text);

#endif