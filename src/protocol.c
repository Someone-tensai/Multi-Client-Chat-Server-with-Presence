#include "protocol.h"
#include <string.h>
#include <stdio.h>

// Clientt Side Parsing to be Implemented by Another Person

// Function to Parse Incoming commands
// Takes a raw string and splits it into
// Command - The Command to Run
// Arguments - Thhe Arguments with the Command
cmd parse_incoming_command_server(char* line)
{
    char delim[] = " ";
    char* command = strtok(line, delim);
    cmd incoming_command = {0};

    if (command == NULL)
    {
        incoming_command.type = TYPE_INVALID;
        return incoming_command;
    }

    if (strcmp(command, CMD_WHO) == 0)
    {
        incoming_command.type = TYPE_WHO;
        // Optional pagination args: WHO <offset> <limit>
        incoming_command.arg1 = strtok(NULL, delim); // offset or NULL
        incoming_command.arg2 = strtok(NULL, delim); // limit or NULL
    }
    else if (strcmp(command, CMD_ROOMS) == 0)
    {
        incoming_command.type = TYPE_ROOMS;
        incoming_command.arg1 = strtok(NULL, delim); // offset
        incoming_command.arg2 = strtok(NULL, delim); // limit
    }
    else if (strcmp(command, CMD_RECONNECT) == 0)
    {
        incoming_command.type = TYPE_RECONNECT;
        incoming_command.arg1 = strtok(NULL, delim); // token
    }
    else if (strcmp(command, CMD_REGISTER) == 0)
    {
        incoming_command.type = TYPE_REGISTER;
        incoming_command.arg1 = strtok(NULL, delim);  // username
        incoming_command.arg2 = strtok(NULL, delim);  // password
    }
    else if (strcmp(command, CMD_LOGIN) == 0)
    {
        incoming_command.type = TYPE_LOGIN;
        incoming_command.arg1 = strtok(NULL, delim);  // username
        incoming_command.arg2 = strtok(NULL, delim);  // password
    }
    else if (strcmp(command, CMD_CREATE) == 0)
    {
        incoming_command.type = TYPE_CREATE;
        incoming_command.arg1 = strtok(NULL, delim);
    }
    else if (strcmp(command, CMD_JOIN) == 0)
    {
        incoming_command.type = TYPE_JOIN;
        incoming_command.arg1 = strtok(NULL, delim);
    }
    else if (strcmp(command, CMD_LEAVE) == 0)
    {
        incoming_command.type = TYPE_LEAVE;
        incoming_command.arg1 = strtok(NULL, delim);
    }
    else if (strcmp(command, CMD_KICK) == 0)
    {
        incoming_command.type = TYPE_KICK;
        incoming_command.arg1 = strtok(NULL, delim);  // target username
    }
    else if (strcmp(command, CMD_PROMOTE) == 0)
    {
        incoming_command.type = TYPE_PROMOTE;
        incoming_command.arg1 = strtok(NULL, delim);  // target username
    }
    else if (strcmp(command, CMD_STATUS) == 0)
    {
        incoming_command.type = TYPE_STATUS;
        incoming_command.arg1 = strtok(NULL, delim);  // ONLINE | AWAY | BUSY
    }
    else if (strcmp(command, CMD_EDIT) == 0)
    {
        incoming_command.type = TYPE_EDIT;
        incoming_command.arg1 = strtok(NULL, delim);  // message_id
        incoming_command.arg2 = strtok(NULL, "");     // new_text
    }
    else if (strcmp(command, CMD_DELETE) == 0)
    {
        incoming_command.type = TYPE_DELETE;
        incoming_command.arg1 = strtok(NULL, delim);  // message_id
    }
    else if (strcmp(command, CMD_HISTORY) == 0)
    {
        incoming_command.type = TYPE_HISTORY;
        incoming_command.arg1 = strtok(NULL, delim);  // room
        incoming_command.arg2 = strtok(NULL, delim);  // cursor (may be NULL)
        incoming_command.arg3 = strtok(NULL, delim);  // limit (may be NULL)
    }
    else if (strcmp(command, CMD_MSG) == 0)
    {
        incoming_command.type = TYPE_MSG;
        incoming_command.arg1 = strtok(NULL, "");
    }
    else if (strcmp(command, CMD_PM) == 0)
    {
        incoming_command.type = TYPE_PM;
        incoming_command.arg1 = strtok(NULL, delim);
        incoming_command.arg2 = strtok(NULL, "");
    }
    else
    {
        incoming_command.type = TYPE_INVALID;
    }

    return incoming_command;
}

// Functions to Format Outgoing Commands
void format_ok_reply(char *out, size_t out_size, const char *status)
{
    snprintf(out, out_size, "%s %s\n", REPLY_OK, status);
}

void format_ok_session(char *out, size_t out_size, const char *status, const char *token)
{
    if (token && token[0] != '\0')
        snprintf(out, out_size, "%s %s %s\n", REPLY_OK, status, token);
    else
        snprintf(out, out_size, "%s %s\n", REPLY_OK, status);
}

void format_err_reply(char *out, size_t out_size, const char *err_code)
{
    snprintf(out, out_size, "%s %s\n", REPLY_ERR, err_code);
}

void format_notice(char *out, size_t out_size, const char *user, const char *action, const char *room)
{
    snprintf(out, out_size, "%s %s %s %s\n", REPLY_NOTICE, user, action, room);
}

void format_msg_reply(char *out, size_t out_size, const char *sender, const char *text)
{
    snprintf(out, out_size, "%s %s %s\n", REPLY_MSG, sender, text);
}

void format_pm_reply(char *out, size_t out_size, const char *sender, const char *text)
{
    snprintf(out, out_size, "%s %s %s\n", REPLY_PM_FROM, sender, text);
}

void format_msg_reply_id(char *out, size_t out_size, long long msg_id, const char *sender, const char *text)
{
    snprintf(out, out_size, "%s %lld %s %s\n", REPLY_MSG, msg_id, sender, text);
}

void format_history_reply(char *out, size_t out_size, const char *room, long long cursor, int count)
{
    snprintf(out, out_size, "%s %s %lld %d\n", REPLY_HISTORY, room, cursor, count);
}

void format_edited_reply(char *out, size_t out_size, long long msg_id, const char *new_text)
{
    snprintf(out, out_size, "%s %lld %s\n", REPLY_EDITED, msg_id, new_text);
}

void format_deleted_reply(char *out, size_t out_size, long long msg_id)
{
    snprintf(out, out_size, "%s %lld\n", REPLY_DELETED, msg_id);
}