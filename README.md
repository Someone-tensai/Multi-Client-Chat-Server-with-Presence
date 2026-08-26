# Multi-Client Chat Server with Presence

A multi-threaded, epoll-based chat server in C with TLS, SQLite persistence, dynamic thread pool, presence, admin commands, rate limiting, and graceful shutdown.

---

## How to Build and Run

**Requirements:** Linux or WSL (Ubuntu), gcc, make, pthreads, sqlite3, openssl

```bash
make          # builds ./server and ./client
make clean    # removes binaries
```

```bash
./server                  # starts server on port 8888 (reads server.conf)
./server myconfig.conf    # starts server with a custom config file
./client                  # connects to 127.0.0.1:8888
./client <host> <port>    # connect to a remote server
```

To build with sanitizers:

```bash
make clean
CFLAGS="-Wall -Wextra -Wpedantic -fsanitize=address,undefined -g" make
```

---

## Configuration

All runtime tuning is via `server.conf` (key=value). Missing keys fall back to compile-time defaults.

| Key | Default | Effect |
|---|---|---|
| `port` | `8888` | Server listening port |
| `thread_pool_size` | `16` | Max thread pool workers (pool scales up to this under load) |
| `pool_min_threads` | `2` | Minimum workers kept alive (pool won't shrink below this) |
| `pool_shrink_idle_sec` | `30` | Seconds of idleness before an excess worker exits |
| `max_clients` | `64` | Max registered users at once (dynamic allocation) |
| `max_rooms` | `16` | Max simultaneous rooms (dynamic) |
| `max_members` | `16` | Max users per room (dynamic) |
| `history_size` | `10` | Messages replayed on JOIN (circular buffer, dynamic) |
| `rate_bucket_max` | `5` | Burst allowance per client (tokens) |
| `rate_refill_rate` | `1.0` | Tokens/second added back |
| `rate_msg_cost` | `1` | Tokens consumed per MSG or PM |
| `tls_cert` | `server.crt` | TLS certificate file path |
| `tls_key` | `server.key` | TLS private key file path |
| `log_level` | `INFO` | Minimum log level: DEBUG, INFO, WARN, ERROR, NONE |
| `log_file` | `""` (empty) | Optional log file path (e.g. `server.log`). If empty, logs to console only |
| `session_expire_sec` | `86400` | Session token lifetime in seconds (24h, compiled default) |

Example `server.conf`:

```ini
port = 8888
thread_pool_size = 16
max_clients = 64
max_rooms = 16
max_members = 16
history_size = 10
log_level = INFO
log_file = server.log
```

**Dynamic sizing:** `max_clients`, `max_rooms`, `max_members`, `history_size` are no longer compile-time constants. `registry_init()` allocates `room_list`, `client_list`, `room->members`, and `room->history` dynamically based on the config. Changing these values in `server.conf` actually resizes the structures (requires server restart).

---

## Authentication & Sessions

### Commands

```
REGISTER <name> <password>   # register new user, returns session token
LOGIN <name> <password>      # login existing user, returns session token
RECONNECT <session_token>    # reconnect with previous session token
```

### Flow

1. `REGISTER` or `LOGIN` authenticates via SQLite (`users` table, SHA-256 hashed passwords) and on success the server creates a secure random session token (32 bytes → 64 hex chars via `RAND_bytes`).

   Response: `OK REGISTERED <token>` or `OK LOGGED_IN <token>` (via `format_ok_session`).

2. Client should save the token. On disconnect, the server preserves the session in the `sessions` DB table; the active `client_t` is removed but the DB session remains valid.

3. To reconnect without re-registering, send `RECONNECT <token>`.

   - Server validates token against `sessions` table (`expires_at` check).
   - On success, it rotates the token: old token deleted, new token created and returned: `OK RECONNECTED <new_token>`.
   - On failure: `ERR INVALID_TOKEN` or `ERR SESSION_EXPIRED`.

4. Sessions survive server restarts (SQLite persistence in `chat.db`).

### Security

- Tokens are cryptographically random (`openssl RAND_bytes`), not `rand()`/timestamp.
- Tokens, passwords, and password hashes are never logged.
- `RECONNECT` requires no password; the token alone authenticates.
- Duplicate active usernames: new LOGIN/RECONNECT for same username is allowed and creates a new `client_t` (old connection will be cleaned on its next disconnect). Old socket is not automatically killed, but the new connection takes over logically.

### DB Schema

```sql
CREATE TABLE users (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL);
CREATE TABLE sessions (
    token TEXT PRIMARY KEY,
    username TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL
);
```

Helper APIs in `db.h`: `db_create_session`, `db_validate_session`, `db_delete_session`, `db_cleanup_expired_sessions`, `db_generate_token`.

---

## Pagination

`WHO` and `ROOMS` now support optional pagination to avoid huge replies.

### Protocol

```
WHO [offset] [limit]
ROOMS [offset] [limit]
```

- No args: `WHO` / `ROOMS` → backwards compatible, returns all (old format: `WHO_REPLY *alice/ONLINE bob/AWAY`, `ROOMS_REPLY room1 room2`).
- With args: `WHO 0 10` → returns page 0 with 10 entries.

Validation: negative offset → 0, zero limit → empty, limit >1000 → capped to 1000, offset beyond total → empty page.

### Response (paginated)

```
WHO_REPLY <total> <offset> <count> [*name/STATUS ...]
ROOMS_REPLY <total> <offset> <count> [room ...]
```

- `total` = total members/rooms available
- `offset` = requested offset (clamped)
- `count` = number of entries returned in this page (0 ≤ count ≤ limit)

Examples:

```
Client: WHO 0 1
Server: WHO_REPLY 3 0 1 *alice/ONLINE

Client: ROOMS 1 2
Server: ROOMS_REPLY 5 1 2 room2 room3

Client: ROOMS
Server: ROOMS_REPLY room1 room2 room3  # old format
```

Client (`replies/client.c`) understands both formats and displays pagination info.

---

## Logging

### Framework

New centralized logger in `include/log.h` / `src/log.c`.

- Levels: `DEBUG < INFO < WARN < ERROR` (plus `NONE` to disable)
- Each line: `2026-08-26 21:30:12 [INFO] src/server.c:247 (run_server) Server listening...`
- Thread-safe via `pthread_mutex_t`.
- Outputs to console (DEBUG/INFO → stdout, WARN/ERROR → stderr) and optionally to a file.

### Configuration

```
log_level = INFO
log_file = server.log   # if empty or absent, console only
```

Filtered: messages below `log_level` are suppressed. Failures to open log file do not crash server (fallback to console).

### Usage in Code

```c
#include "log.h"
LOG_DEBUG("MSG from %s", user);
LOG_INFO("New client fd=%d", fd);
LOG_WARN("Token collision, retrying");
LOG_ERROR("db_open: %s", msg);
LOG_ERROR_ERRNO("accept failed");
```

Replaced scattered `printf`/`perror`/`fprintf` in server-side code (`server.c`, `db.c`, `threadpool.c`). `display.c` remains for client UI (not server logging).

Never logged: passwords, password hashes, session tokens, private message *contents* (only debug metadata).

---

## Protocol

Text-based, newline-terminated, case-sensitive.

### Client → Server

| Command | Description |
|---|---|
| `REGISTER <name> <password>` | Register username with password (must be first command) |
| `LOGIN <name> <password>` | Login with existing credentials, returns session token |
| `RECONNECT <token>` | Reconnect with session token, rotates token |
| `CREATE <room>` | Create a room and join it |
| `JOIN <room>` | Join an existing room |
| `LEAVE` | Leave current room |
| `MSG <text>` | Broadcast message to room |
| `PM <user> <text>` | Private message to a user |
| `WHO [offset] [limit]` | List members and their status in current room (paginated) |
| `ROOMS [offset] [limit]` | List all active rooms (paginated) |
| `STATUS ONLINE\|AWAY\|BUSY` | Set your presence status |
| `KICK <user>` | Remove a user from room (admin only) |
| `PROMOTE <user>` | Transfer admin role (admin only) |

### Server → Client

| Reply | Meaning |
|---|---|
| `OK REGISTERED <token>` | Register succeeded, token returned |
| `OK LOGGED_IN <token>` | Login succeeded, token returned |
| `OK RECONNECTED <new_token>` | Reconnect succeeded, new token returned |
| `OK <status>` | Other commands succeeded (CREATED, JOINED, LEFT, etc.) |
| `ERR <code>` | Command failed (e.g. `INVALID_TOKEN`, `SESSION_EXPIRED`, `RATE_LIMITED`) |
| `NOTICE <user> <action> <room>` | Presence event (joined, left, kicked, promoted, status change) |
| `NOTICE history START <room>` / `END` | History replay delimiters |
| `MSG <sender> <text>` | Room message |
| `PM_FROM <sender> <text>` | Private message |
| `WHO_REPLY [<total> <offset> <count>] <*name/STATUS> ...` | WHO response (`*` = admin), with pagination header if requested |
| `ROOMS_REPLY [<total> <offset> <count>] <room> ...` | ROOMS response, with pagination header if requested |

---

## Architecture

```
config_load("server.conf")     ← parses key=value, populates server_config_t (includes log_level/log_file)
       │
       ▼
   log_init(level, file)       ← centralized, thread-safe logger
       │
       ▼
registry_init(cfg)             ← dynamic allocation: room_list, client_list, members, history
       │
       ▼
   db_open()                   ← SQLite WAL, users + messages + sessions tables
       │
       ▼
accept loop (main thread)      ← epoll_wait, non-blocking I/O
       │
       ▼
   threadpool (dynamic)         ← scales up on load, shrinks on idle, configured via server.conf
       │                           logs via LOG_INFO
       ▼
handle_client(conn)            ← one worker per burst of data, transparent TLS via conn_send/conn_recv
       │                           parses via parse_incoming_command_server()
       ├── registry functions (create_room, room_add_member, etc.)  ← uses registry_lock + room_lock
       ├── db_* functions (SQLite persistence, user auth, sessions) ← db_lock mutex
       ├── session handling (RECONNECT, token rotation)              ← RAND_bytes, sessions table
       ├── pagination (WHO/ROOMS offset/limit)                       ← snapshot under lock, no huge temp strings
       └── format_*_reply() + conn_send() → also LOG_DEBUG where appropriate

Global state (registry.c, dynamic):
  pthread_rwlock_t registry_lock   — guards room_list[] and client_list[] (dynamic arrays)
  room_t **room_list; int room_capacity; int room_count;
  client_t **client_list; int client_capacity; int client_count;

Per-room state (room_t, dynamic):
  pthread_mutex_t room_lock        — guards members[] and history[]
  client_t **members; int member_capacity; int member_count;
  message_t *history; int history_capacity; int history_count (total) + history_start; // circular buffer, replayed on JOIN, survives restart via DB

Database (db.c → chat.db, SQLite WAL, db_lock mutex):
  messages table — persisted room message history
  users table    — username + SHA-256 password hash
  sessions table — token, username, created_at, expires_at (token rotation on reconnect)

Logging (log.c, log_lock mutex):
  console + optional file, level filtering, timestamp, file:line (func)
```

### Locking rules

- **registry_lock (rwlock):** read-lock for `find_room` / `find_client`. Write-lock for `create_room` / `delete_room` / `create_client` / `delete_client`. Never hold together with `room->room_lock`.
- **room->room_lock (mutex):** held for any access to `room->members[]`, `room->history[]`, `room->member_count`, etc.
- **db_lock (mutex):** inside `db.c` for all SQLite operations.
- **log_lock (mutex):** inside `log.c` for console/file writes.
- **Never hold both registry_lock and room_lock simultaneously** to avoid deadlock.

---

## Project Structure

```
include/
  common.h        — shared includes (stdio, socket, netinet, etc.)
  protocol.h      — command/reply string constants, cmd struct, session token defs, format helpers
  registry.h      — client_t, room_t, message_t, dynamic capacities, registry lifecycle APIs
  server.h        — run_server(), handle_client() declarations
  display.h       — display_*() function declarations (client UI, not server logging)
  threadpool.h    — threadpool_t struct and API (dynamic resize)
  config.h        — server_config_t struct, config_load(), config_get(), log/session defaults
  db.h            — SQLite persistence API + session APIs
  conn.h          — per-connection state (epoll, TLS, partial buffer)
  log.h           — logger API, levels, macros LOG_DEBUG/INFO/WARN/ERROR

src/
  server.c        — main(), run_server(), SIGINT handler, config + log + registry integration, TLS greeting
  client_handler.c — per-client command dispatch loop (all commands + RECONNECT + pagination)
  registry.c      — dynamic room/client storage, lifecycle (registry_init/destroy, room_destroy), circular history
  protocol.c      — parse_incoming_command_server(), format_*_reply() (including session, pagination)
  display.c       — color-coded terminal output (client presentation, separate from server logging)
  threadpool.c    — dynamic worker pool (auto-scale + shrink) with LOG_INFO
  config.c        — key=value parser (including log_level/log_file)
  db.c            — SQLite (messages, users, sessions with secure token generation)
  log.c           — thread-safe logger with timestamp, level, file output

replies/
  client.c        — full client program (connects, TLS greeting, handles paginated replies, session token display)

tests/
  test_registry.c — unit tests for registry (including dynamic capacities, wrap-around, zero capacity)
  integration_test.py — end-to-end tests (register/login/reconnect, pagination, history, rate limit, logging)

server.conf       — runtime config file (key=value, loaded at startup, includes log settings)
```

---

## Testing

### Unit

```bash
gcc -Wall -Wextra -I include -o /tmp/test_registry tests/test_registry.c src/registry.c src/protocol.c src/display.c src/db.c src/config.c src/log.c -lpthread -lsqlite3 -lssl -lcrypto
/tmp/test_registry
```

Covers: `max_clients=2`, `max_rooms=2`, `max_members=2`, `history_size=2`, limits, deletion/recreation, wrap-around, zero capacity, invalid config fallback.

### Integration

```bash
python3 tests/integration_test.py
```

Covers: register → token, login → token, disconnect → reconnect with valid/invalid/expired/rotated token, room creation to capacity, ROOMS/WHO pagination (offset/limit, beyond total, large limit), history replay (in-memory + persisted after recreate), presence, rate limiting, logging file, graceful shutdown, concurrent clients.

### Stress

The integration test also exercises concurrent connects/disconnects, room creation/deletion, WHO/ROOMS concurrency. For deeper stress, run with sanitizers:

```bash
CFLAGS="-fsanitize=address,undefined -g" make clean && make
./server &
# then run multiple clients or python stress loops
```

---

## Known Limitations & Future

- `session_expire_sec` is currently a compile-time constant (`SESSION_EXPIRE_SEC 86400`) — could be made configurable via `server.conf` (add `session_expire_sec` key).
- No explicit logout that deletes session; sessions expire automatically or are rotated on reconnect. `db_cleanup_expired_sessions()` is called on validate and could be called periodically.
- `WHO`/`ROOMS` pagination uses in-memory snapshot under lock — safe for current limits, but for very large deployments, consider streaming.
- Rate limiting still uses compile-time constants `RATE_*` in `registry.h` (could be made dynamic via `server_config_t`).
- Valgrind/ThreadSanitizer not enabled in default build; use sanitizers for deeper auditing.

---

## Tuning Constants

All configurable via `server.conf` at runtime. Defaults below are used when the config file is absent or a key is missing.

| Key | Config Default | Effect |
|---|---|---|
| `port` | `8888` | Server listening port |
| `thread_pool_size` | `16` | Max thread pool workers (pool scales up to this under load) |
| `pool_min_threads` | `2` | Minimum workers kept alive (pool won't shrink below this) |
| `pool_shrink_idle_sec` | `30` | Seconds of idleness before an excess worker exits |
| `max_clients` | `64` | Max registered users at once |
| `max_rooms` | `16` | Max simultaneous rooms |
| `max_members` | `16` | Max users per room |
| `history_size` | `10` | Messages replayed on JOIN |
| `rate_bucket_max` | `5` | Burst allowance per client (tokens) |
| `rate_refill_rate` | `1.0` | Tokens/second added back |
| `rate_msg_cost` | `1` | Tokens consumed per MSG or PM |
| `tls_cert` | `server.crt` | TLS certificate file path |
| `tls_key` | `server.key` | TLS private key file path |
| `log_level` | `INFO` | Minimum log level |
| `log_file` | `""` | Log file path (empty = console only) |
