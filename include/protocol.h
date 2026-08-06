// To ensure no circular includes
#ifndef PROTOCOL_H
#define PROTOCOL_H

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
#define ERR_USERNAME_TAKEN "USERNAME_TAKEN"
#define ERR_INVALID_USERNAME "INVALID_USERNAME"
#define ERR_NOT_REGISTERED "NOT_REGISTERED"
#define ERR_ALREADY_REGISTERED "ALREADY_REGISTERED"
#define ERR_UNKNOWN_USER "UNKNOWN_USER"

#define ERR_ROOM_NOT_FOUND "ROOM_NOT_FOUND"
#define ERR_ROOM_ALREADY_EXISTS "ROOM_ALREADY_EXISTS"
#define ERR_MAX_ROOM_COUNT_REACHED "MAX_ROOM_COUNT_REACHED"
#define ERR_ROOM_MALLOC_ERROR "ROOM_MALLOC_ERROR"
#define ERR_NOT_IN_ROOM "NOT_IN_ROOM"
#define ERR_ROOM_NOT_FOUND "ROOM_NOT_FOUND"
#define ERR_ROOM_NULL "ROOM_NULL"
#define ERR_ROOM_MAX_MEMBER_COUNT_REACHED "ROOM_MAX_MEMBERS_REACHED"
#define ERR_ROOM_INVALID_CLIENT "ROOM_INVALID_CLIENT"

#define ERR_EMPTY_MESSAGE "EMPTY_MESSAGE"
#define ERR_LINE_TOO_LONG "LINE_TOO_LONG"

#define ERR_UNKNOWN_COMMAND "UNKNOWN_COMMAND"
#define ERR_MALFORMED "MALFORMED"

#endif