// Client Side Code
#include "../include/common.h"
#include "../include/protocol.h"
#include "../include/display.h"
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// ─────────────────────────────────────────────
// Shared state
// ─────────────────────────────────────────────
static int    server_fd   = -1;
static SSL   *ssl         = NULL;    // NULL in plain-text mode
static char   current_room[MAX_ROOM_NAME_LEN] = "";
static volatile int running = 1;

// ─────────────────────────────────────────────
// TLS — read the server's 1-byte protocol greeting
// to decide TLS vs plain-text, then handshake if needed.
//
// Protocol:
//   Server sends 'T' if TLS is enabled, 'P' if plain-text.
//   Client reads this byte FIRST, then acts accordingly.
// ─────────────────────────────────────────────
static SSL *tls_connect(int fd)
{
    // Read 1-byte protocol greeting from server
    char greeting;
    ssize_t n = recv(fd, &greeting, 1, 0);  // blocking read
    if (n != 1)
        return NULL;  // connection error or closed

    if (greeting == 'P')
        return NULL;  // server is plain-text, no TLS needed

    if (greeting != 'T')
        return NULL;  // unknown greeting, assume plain-text

    // Server says TLS — perform handshake
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    // Accept self-signed certs (the server uses one by default)
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    SSL *s = SSL_new(ctx);
    SSL_CTX_free(ctx);   // SSL holds a reference; CTX can be freed now
    if (!s) return NULL;

    SSL_set_fd(s, fd);
    if (SSL_connect(s) <= 0)
    {
        // TLS handshake failed — disconnect, don't fall back
        // (server expects TLS but we can't speak it)
        printf("[info] TLS handshake failed — server expects TLS\n");
        SSL_free(s);
        return NULL;
    }
    return s;
}

// ─────────────────────────────────────────────
// Thin wrappers so receiver and sender don't
// have to care whether TLS is active.
// ─────────────────────────────────────────────
static ssize_t client_recv(char *buf, size_t len)
{
    if (ssl) return (ssize_t)SSL_read(ssl, buf, (int)len);
    return recv(server_fd, buf, len, 0);
}

static ssize_t client_send(const char *buf, size_t len)
{
    if (ssl) return (ssize_t)SSL_write(ssl, buf, (int)len);
    return send(server_fd, buf, len, 0);
}

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

    // ── OK <status> [token] ───────────────────
    if (strcmp(first, REPLY_OK) == 0)
    {
        char *status = next_token(&rest, " ");
        if (status == NULL) { display_system("OK"); return; }

        // Check for optional session token after status (for REGISTER/LOGIN/RECONNECT)
        char *token = next_token(&rest, " ");

        if (strcmp(status, OK_REGISTERED) == 0) {
            if (token) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Registered successfully. Session token: %s (save for RECONNECT)", token);
                display_system(buf);
            } else {
                display_system("Registered successfully.");
            }
        }
        else if (strcmp(status, OK_LOGGED_IN) == 0) {
            if (token) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Logged in successfully. Session token: %s", token);
                display_system(buf);
            } else {
                display_system("Logged in successfully.");
            }
        }
        else if (strcmp(status, OK_RECONNECTED) == 0) {
            if (token) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Reconnected successfully. New token: %s", token);
                display_system(buf);
            } else {
                display_system("Reconnected successfully.");
            }
        }
        else if (strcmp(status, OK_SESSION) == 0) {
            if (token) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Session token: %s", token);
                display_system(buf);
            }
        }
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
        else if (strcmp(status, REPLY_LOGOUT) == 0)
            display_system("Logged out.");
        else if (strcmp(status, OK_SENT) == 0)
        {
            // Silent — no need to echo "sent" back to user
        }
        else
        {
            char buf[256];
            if (token)
                snprintf(buf, sizeof(buf), "OK %s %s", status, token);
            else
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

    // ── MSG [<id>] <sender> <text> ───────────────────
    else if (strcmp(first, REPLY_MSG) == 0)
    {
        char *first_tok = next_token(&rest, " ");
        char *text = NULL;
        char *sender = NULL;
        char *id_str = NULL;
        if (first_tok) {
            // Check if first_tok is numeric (message_id)
            char *end;
            strtoll(first_tok, &end, 10);
            if (*end=='\0' && rest && *rest) {
                // Numeric -> treat as id
                id_str = first_tok;
                sender = next_token(&rest, " ");
                text = rest;
            } else {
                // Old format: first_tok is sender
                sender = first_tok;
                text = rest;
            }
        }
        if (sender && text) {
            // Optionally show id in debug
            if (id_str) {
                char disp_text[MAX_TEXT_LEN+32];
                snprintf(disp_text, sizeof(disp_text), "[%s] %s", id_str, text);
                display_message(current_room[0] ? current_room : "?", sender, disp_text);
            } else {
                display_message(current_room[0] ? current_room : "?", sender, text);
            }
        }
    }
    else if (strcmp(first, REPLY_EDITED) == 0)
    {
        char *id = next_token(&rest, " ");
        char *new_text = rest;
        if (id && new_text) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Message %s edited: %s", id, new_text);
            display_notice(buf);
        }
    }
    else if (strcmp(first, REPLY_DELETED) == 0)
    {
        char *id = next_token(&rest, " ");
        if (id) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Message %s deleted", id);
            display_notice(buf);
        }
    }
    else if (strcmp(first, REPLY_HISTORY) == 0)
    {
        char *room = next_token(&rest, " ");
        char *cursor = next_token(&rest, " ");
        char *cnt = next_token(&rest, " ");
        char buf[256];
        snprintf(buf, sizeof(buf), "History %s cursor=%s count=%s", room?room:"?", cursor?cursor:"0", cnt?cnt:"0");
        display_system(buf);
        // Following MSG lines will be displayed as normal MSG
    }

    // ── PM_FROM <sender> <text> ───────────────
    else if (strcmp(first, REPLY_PM_FROM) == 0)
    {
        char *sender = next_token(&rest, " ");
        char *text   = rest;
        if (sender && text)
            display_pm(sender, text);
    }

    // ── SESSIONS_REPLY <count> [tokens...] ─────────────
    else if (strcmp(first, REPLY_SESSIONS) == 0)
    {
        char *count_str = next_token(&rest, " ");
        int count = count_str ? atoi(count_str) : 0;
        char buf[MAX_LINE_LEN];
        if (count == 0) {
            display_system("No active sessions.");
        } else {
            int off = snprintf(buf, sizeof(buf), "Active sessions (%d):", count);
            char *tok;
            while ((tok = next_token(&rest, " ")) != NULL) {
                off += snprintf(buf + off, sizeof(buf) - off, " %s", tok);
            }
            display_system(buf);
        }
    }

    // ── READ_RECEIPT <msg_id> <reader> ────────────────
    else if (strcmp(first, REPLY_READ_RECEIPT) == 0)
    {
        char *msg_id = next_token(&rest, " ");
        char *reader = next_token(&rest, " ");
        if (msg_id && reader) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s read message #%s", reader, msg_id);
            display_notice(buf);
        }
    }

    // ── TYPING <user> ─────────────────────────────────
    else if (strcmp(first, REPLY_TYPING) == 0)
    {
        char *user = next_token(&rest, " ");
        if (user) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s is typing...", user);
            display_notice(buf);
        }
    }

    // ── STOP_TYPING <user> ────────────────────────────
    else if (strcmp(first, REPLY_STOP_TYPING) == 0)
    {
        char *user = next_token(&rest, " ");
        if (user) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s stopped typing.", user);
            display_notice(buf);
        }
    }

    // ── LOGOUT_OK ──────────────────────────────────────
    else if (strcmp(first, REPLY_LOGOUT) == 0)
    {
        display_system("Logged out.");
    }

    // ── END_HISTORY ────────────────────────────────────
    else if (strcmp(first, "END_HISTORY") == 0)
    {
        display_system("--- end of history ---");
    }

    // ── WHO_REPLY [<total> <offset> <count>] <name>/<status> ... ─
    // Admin is prefixed with *, pagination numbers if present
    else if (strcmp(first, REPLY_WHO) == 0)
    {
        char buf[MAX_LINE_LEN];
        int off = 0;
        // Peek if next 3 tokens are numeric (pagination)
        char *save = rest;
        char *t1 = next_token(&rest, " ");
        char *t2 = NULL;
        char *t3 = NULL;
        int is_paginated = 0;
        int total = 0, offset = 0, count = 0;
        if (t1) {
            char *e1;
            total = (int)strtol(t1, &e1, 10);
            if (*e1 == '\0') {
                t2 = next_token(&rest, " ");
                if (t2) {
                    char *e2;
                    offset = (int)strtol(t2, &e2, 10);
                    if (*e2 == '\0') {
                        t3 = next_token(&rest, " ");
                        if (t3) {
                            char *e3;
                            count = (int)strtol(t3, &e3, 10);
                            if (*e3 == '\0') {
                                is_paginated = 1;
                            }
                        }
                    }
                }
            }
        }
        if (is_paginated) {
            off = snprintf(buf, sizeof(buf), "Users in room (%d total, %d-%d):", total, offset, offset+count-1 < 0 ? 0 : offset+count-1);
        } else {
            // Not paginated — rewind and treat all as entries
            rest = save;
            off = snprintf(buf, sizeof(buf), "Users in room:");
        }
        char *entry;
        while ((entry = next_token(&rest, " ")) != NULL)
        {
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
        if (is_paginated && count == 0) {
            off += snprintf(buf + off, sizeof(buf) - off, " (no entries in this page)");
        }
        display_system(buf);
    }

    // ── ROOMS_REPLY [<total> <offset> <count>] <room1> <room2> ... ───
    else if (strcmp(first, REPLY_ROOMS) == 0)
    {
        char buf[MAX_LINE_LEN];
        // Try to parse pagination header
        char *save = rest;
        char *t1 = next_token(&rest, " ");
        char *t2 = NULL;
        char *t3 = NULL;
        int is_paginated = 0;
        int total = 0, offset = 0, count = 0;
        if (t1) {
            char *e1;
            total = (int)strtol(t1, &e1, 10);
            if (*e1 == '\0') {
                t2 = next_token(&rest, " ");
                if (t2) {
                    char *e2;
                    offset = (int)strtol(t2, &e2, 10);
                    if (*e2 == '\0') {
                        t3 = next_token(&rest, " ");
                        if (t3) {
                            char *e3;
                            count = (int)strtol(t3, &e3, 10);
                            if (*e3 == '\0') {
                                is_paginated = 1;
                            }
                        }
                    }
                }
            }
        }
        if (is_paginated) {
            int off = snprintf(buf, sizeof(buf), "Active rooms (%d total, offset %d, %d shown):", total, offset, count);
            char *rname;
            int added = 0;
            while ((rname = next_token(&rest, " ")) != NULL)
            {
                off += snprintf(buf + off, sizeof(buf) - off, " %s", rname);
                added++;
            }
            if (added == 0) {
                snprintf(buf + off, sizeof(buf) - off, " (none)");
            }
            display_system(buf);
        } else {
            // Not paginated — rewind
            rest = save;
            int off = snprintf(buf, sizeof(buf), "Active rooms:");
            char *rname;
            while ((rname = next_token(&rest, " ")) != NULL)
            {
                off += snprintf(buf + off, sizeof(buf) - off, " %s", rname);
            }
            display_system(buf);
        }
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

    while (running && (n = client_recv(buf, sizeof(buf) - 1)) > 0)
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
    display_system("  REGISTER <name> <pass> - register with password");
    display_system("  LOGIN <name> <pass>    - login with existing account");
    display_system("  RECONNECT <token>      - reconnect with session token");
    display_system("  CREATE <room>        - create and join a room");
    display_system("  JOIN <room>          - join an existing room");
    display_system("  LEAVE                - leave current room");
    display_system("  MSG <text>           - send message to room");
    display_system("  PM <user> <text>     - send private message");
    display_system("  WHO [offset] [limit] - list users in room (paginated)");
    display_system("  ROOMS [offset] [limit] - list all rooms (paginated)");
    display_system("  KICK <user>          - kick a user (admin only)");
    display_system("  PROMOTE <user>       - make a user admin (admin only)");
    display_system("  STATUS <ONLINE|AWAY|BUSY> - set your presence status");
    display_system("  EDIT <id> <text>     - edit own message (by id)");
    display_system("  DELETE <id>          - delete own message (admin can delete any)");
    display_system("  HISTORY <room> [cursor] [limit] - paginated history (cursor=id)");
    display_system("  SEARCH <room> <query> - search messages in room");
    display_system("  READ <msg_id>        - mark message as read (receipt)");
    display_system("  TYPING               - broadcast typing indicator");
    display_system("  STOP_TYPING          - stop typing indicator");
    display_system("  SESSIONS             - list your active sessions");
    display_system("  REVOKE_SESSION <token> - revoke a session");
    display_system("  LOGOUT               - logout current session");
    display_system("  LOGOUT_ALL           - logout all sessions");
    display_system("  /help                - show this help");
    display_system("  /quit                - disconnect and exit");
    display_system("Pagination: e.g. WHO 0 10, ROOMS 0 10, HISTORY demoRoom 0 20");
    display_system("Session: after REGISTER/LOGIN, save the token for RECONNECT");
    display_system("Message IDs shown as [id] in MSG, use for EDIT/DELETE");
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

    // Attempt TLS handshake; fall back to plain-text if server is plain
    ssl = tls_connect(server_fd);
    if (ssl)
        display_system("TLS connection established.");
    else
        display_system("Plain-text connection (TLS not available).");

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
        if (client_send(out, strlen(out)) < 0)
        {
            display_system("Failed to send. Disconnected?");
            break;
        }
    }

    running = 0;
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
    close(server_fd);
    return 0;
}
