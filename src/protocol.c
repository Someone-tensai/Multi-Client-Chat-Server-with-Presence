#include "protocol.h"
#include <string.h>
#include <stdio.h>

// Function to Parse Incoming commands
// Takes a raw string and splits it into
// Command - The Command to Run
// Arguments - Thhe Arguemnts with the Command
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
    }
    else if (strcmp(command, CMD_ROOMS) == 0)
    {
        incoming_command.type = TYPE_ROOMS;
    }
    else if (strcmp(command, CMD_REGISTER) == 0)
    {
        incoming_command.type = TYPE_REGISTER;
        incoming_command.arg1 = strtok(NULL, delim);
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