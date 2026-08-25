// Client Side Code
#include "../include/common.h"
#include "../include/protocol.h"
#include "../include/display.h"
#include <pthread.h>

// ─────────────────────────────────────────────
// Shared state
// ─────────────────────────────────────────────
static int    server_fd   = -1;          // socket to the server
static char   current_room[MAX_ROOM_NAME_LEN] = "";   // track current room for display
static volatile int running = 1;         // set to 0 to stop both threads

// ─────────────────────────────────────────────
// Helper: parse one token from a string
// ─────────────────────────────────────────────
static char *next_token(char **cursor, const char *delim)
{
    return strtok_r(*cursor, delim, cursor);
}

// ─────────────────────────────────────────────
// Parse a line from the server and display it
// ─────────────────────────────────────────────
static void handle_server_line(char *line)
{
    // Strip trailing \r\n
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
        line[--len] = '\0';

    if (len == 0) return;

    char *rest  = line;
    char *first = next_token(&rest, " ");
    if (first == NULL) return;

    // ── OK <status> ───────────────────────────
    if (strcmp(first, REPLY_OK) == 0)
    {
        char *status = next_token(&rest, " ");
        if (status == NULL) { display_system("OK"); return; }

        if (strcmp(status, OK_REGISTERED) == 0)
            display_system("Registered successfully.");
        else if (strcmp(status, OK_CREATED) == 0)
            display_system("Room created. You are now in it.");
        else if (strcmp(status, OK_JOINED) == 0)
            display_system("Joined room.");
        else if (strcmp(status, OK_LEFT) == 0)
        {
            display_system("Left the room.");
            current_room[0] = '\0';
        }
        else if (strcmp(status, OK_KICKED) == 0)
            display_system("User kicked.");
        else if (strcmp(status, OK_PROMOTED) == 0)
            display_system("User promoted to admin.");
        else if (strcmp(status, OK_STATUS_SET) == 0)
            display_system("Status updated.");
        else if (strcmp(status, OK_SENT) == 0)
        {
            // Silent — no need to echo "sent" back to user
        }
        else
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "OK %s", status);
            display_system(buf);
        }
    }

    // ── ERR <code> ────────────────────────────
    else if (strcmp(first, REPLY_ERR) == 0)
    {
        char *code = next_token(&rest, " ");
        // Special case: server sends ERR KICKED when the client is the one being kicked
        if (code && strcmp(code, OK_KICKED) == 0)
        {
            display_system("You have been kicked from the room.");
            current_room[0] = '\0';
        }
        else if (code && strcmp(code, ERR_SERVER_SHUTDOWN) == 0)
        {
            display_system("Server is shutting down. Goodbye.");
            running = 0;
        }
        else if (code && strcmp(code, ERR_RATE_LIMITED) == 0)
        {
            display_error("Slow down — you are sending messages too fast.");
        }
        else
        {
            display_error(code ? code : "UNKNOWN");
        }
    }

    // ── NOTICE <user> <action> <room> ─────────
    else if (strcmp(first, REPLY_NOTICE) == 0)
    {
        char *user   = next_token(&rest, " ");
        char *action = next_token(&rest, " ");
        char *room   = next_token(&rest, " ");

        if (user && action && room)
        {
            char buf[256];

            // History separator — keep out of the join/leave flow
            if (strcmp(user, "history") == 0)
            {
                if (strcmp(action, "START") == 0)
                    snprintf(buf, sizeof(buf), "--- last messages in %s ---", room);
                else
                    snprintf(buf, sizeof(buf), "--- end of history ---");
                display_system(buf);
            }
            else if (strcmp(action, OK_JOINED) == 0)
            {
                strncpy(current_room, room, MAX_ROOM_NAME_LEN - 1);
                snprintf(buf, sizeof(buf), "%s joined %s", user, room);
                display_notice(buf);
            }
            else if (strcmp(action, OK_LEFT) == 0)
            {
                snprintf(buf, sizeof(buf), "%s left %s", user, room);
                display_notice(buf);
            }
            else if (strcmp(action, OK_KICKED) == 0)
            {
                snprintf(buf, sizeof(buf), "%s was kicked from %s", user, room);
                display_notice(buf);
            }
            else if (strcmp(action, OK_PROMOTED) == 0)
            {
                snprintf(buf, sizeof(buf), "%s is now admin of %s", user, room);
                display_notice(buf);
            }
            else if (strcmp(action, STATUS_ONLINE) == 0 ||
                     strcmp(action, STATUS_AWAY)   == 0 ||
                     strcmp(action, STATUS_BUSY)   == 0)
            {
                snprintf(buf, sizeof(buf), "%s is now %s", user, action);
                display_notice(buf);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%s %s %s", user, action, room);
                display_notice(buf);
            }
        }
    }

    // ── MSG <sender> <text> ───────────────────
    else if (strcmp(first, REPLY_MSG) == 0)
    {
        char *sender = next_token(&rest, " ");
        char *text   = rest;   // rest of line is the message
        if (sender && text)
            display_message(current_room[0] ? current_room : "?", sender, text);
    }

    // ── PM_FROM <sender> <text> ───────────────
    else if (strcmp(first, REPLY_PM_FROM) == 0)
    {
        char *sender = next_token(&rest, " ");
        char *text   = rest;
        if (sender && text)
            display_pm(sender, text);
    }

    // ── WHO_REPLY <name>/<status> ... ─────────
    // Admin is prefixed with *
    else if (strcmp(first, REPLY_WHO) == 0)
    {
        char buf[MAX_LINE_LEN];
        int  off = snprintf(buf, sizeof(buf), "Users in room:");
        char *entry;
        while ((entry = next_token(&rest, " ")) != NULL)
        {
            // entry is like "alice/ONLINE" or "*bob/AWAY"
            int is_admin = (entry[0] == '*');
            char *name   = is_admin ? entry + 1 : entry;
            char *slash  = strchr(name, '/');
            if (slash)
            {
                *slash = '\0';
                char *st = slash + 1;
                off += snprintf(buf + off, sizeof(buf) - off,
                                is_admin ? " [*%s/%s]" : " [%s/%s]", name, st);
            }
            else
            {
                off += snprintf(buf + off, sizeof(buf) - off,
                                is_admin ? " [*%s]" : " [%s]", name);
            }
        }
        display_system(buf);
    }

    // ── ROOMS_REPLY <room1> <room2> ... ───────
    else if (strcmp(first, REPLY_ROOMS) == 0)
    {
        char buf[MAX_LINE_LEN] = "Active rooms:";
        char *rname;
        while ((rname = next_token(&rest, " ")) != NULL)
        {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, rname, sizeof(buf) - strlen(buf) - 1);
        }
        display_system(buf);
    }

    // ── Unknown ───────────────────────────────
    else
    {
        display_system(line);
    }
}

// ─────────────────────────────────────────────
// Receiver thread — listens for server messages
// ─────────────────────────────────────────────
static void *receiver_thread(void *arg)
{
    (void)arg;
    char buf[READ_BUFFER_SIZE];
    ssize_t n;

    while (running && (n = recv(server_fd, buf, sizeof(buf) - 1, 0)) > 0)
    {
        buf[n] = '\0';

        // Server may send multiple lines in one recv — handle each
        char *line = strtok(buf, "\n");
        while (line != NULL)
        {
            handle_server_line(line);
            line = strtok(NULL, "\n");
        }
    }

    if (running)
    {
        display_system("Disconnected from server.");
        running = 0;
    }
    return NULL;
}

// ─────────────────────────────────────────────
// Print usage help
// ─────────────────────────────────────────────
static void print_help(void)
{
    display_system("Commands:");
    display_system("  REGISTER <name>      - register your username");
    display_system("  CREATE <room>        - create and join a room");
    display_system("  JOIN <room>          - join an existing room");
    display_system("  LEAVE                - leave current room");
    display_system("  MSG <text>           - send message to room");
    display_system("  PM <user> <text>     - send private message");
    display_system("  WHO                  - list users in room");
    display_system("  ROOMS                - list all rooms");
    display_system("  KICK <user>          - kick a user (admin only)");
    display_system("  PROMOTE <user>       - make a user admin (admin only)");
    display_system("  STATUS <ONLINE|AWAY|BUSY> - set your presence status");
    display_system("  /help                - show this help");
    display_system("  /quit                - disconnect and exit");
}

// ─────────────────────────────────────────────
// Main — connect to server, start receiver thread,
//        read user input and send to server
// ─────────────────────────────────────────────
int main(int argc, char *argv[])
{
    const char *host = DEFAULT_HOST;
    int         port = DEFAULT_PORT;

    // Optional: ./client <host> <port>
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    // Build server address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        return 1;
    }

    // Connect
    if (connect(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        return 1;
    }

    display_system("Connected to chat server. Type /help for commands.");

    // Start receiver thread
    pthread_t tid;
    pthread_create(&tid, NULL, receiver_thread, NULL);
    pthread_detach(tid);

    // Main thread: read user input, send to server
    char input[READ_BUFFER_SIZE];
    while (running && fgets(input, sizeof(input), stdin) != NULL)
    {
        // Strip trailing newline
        int len = strlen(input);
        while (len > 0 && (input[len-1] == '\n' || input[len-1] == '\r'))
            input[--len] = '\0';

        if (len == 0) continue;

        // Local commands
        if (strcmp(input, "/quit") == 0)
        {
            display_system("Goodbye.");
            break;
        }
        if (strcmp(input, "/help") == 0)
        {
            print_help();
            continue;
        }

        // Track room locally for display purposes
        if (strncmp(input, "CREATE ", 7) == 0)
            strncpy(current_room, input + 7, MAX_ROOM_NAME_LEN - 1);
        else if (strncmp(input, "JOIN ", 5) == 0)
            strncpy(current_room, input + 5, MAX_ROOM_NAME_LEN - 1);

        // Send to server (append \n so server gets a complete line)
        char out[READ_BUFFER_SIZE + 2];
        snprintf(out, sizeof(out), "%s\n", input);
        if (send(server_fd, out, strlen(out), 0) < 0)
        {
            display_system("Failed to send. Disconnected?");
            break;
        }
    }

    running = 0;
    close(server_fd);
    return 0;
}
