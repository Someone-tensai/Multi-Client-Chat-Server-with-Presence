# Handover: Multi-Client Chat Server with Presence

This document is for whoever picks this project up next. It covers the current state of the codebase, the architecture, what has been implemented, known limitations, and what to do next to scale it up.

---

## How to Build and Run

**Requirements:** Linux or WSL (Ubuntu), gcc, make, pthreads (included with glibc)

```bash
make          # builds ./server and ./client
make clean    # removes binaries
```

```bash
./server                  # starts server on port 8888
./client                  # connects to 127.0.0.1:8888
./client <host> <port>    # connect to a remote server
```

---

## Project Structure

```
include/
  common.h        — shared includes (stdio, socket, netinet, etc.)
  protocol.h      — command/reply string constants, cmd struct, function declarations
  registry.h      — client_t, room_t, message_t structs, all registry API declarations
  server.h        — run_server(), handle_client() declarations
  display.h       — display_*() function declarations
  threadpool.h    — threadpool_t struct and API

src/
  server.c        — main(), run_server(), SIGINT handler
  client_handler.c — per-client command dispatch loop (all commands implemented)
  registry.c      — room and client state: create, find, add, remove, broadcast, delete
  protocol.c      — parse_incoming_command_server(), format_*_reply() functions
  display.c       — color-coded terminal output functions
  threadpool.c    — fixed worker thread pool

replies/
  client.c        — full client program (connects, sends commands, displays output)

tests/
  test_registry.c — unit tests for registry functions
```

---

## Protocol

Text-based, newline-terminated. All commands are case-sensitive.

### Client → Server

| Command | Description |
|---|---|
| `REGISTER <name>` | Register username (must be first command) |
| `CREATE <room>` | Create a room and join it |
| `JOIN <room>` | Join an existing room |
| `LEAVE` | Leave current room |
| `MSG <text>` | Broadcast message to room |
| `PM <user> <text>` | Private message to a user |
| `WHO` | List members and their status in current room |
| `ROOMS` | List all active rooms |
| `STATUS ONLINE\|AWAY\|BUSY` | Set your presence status |
| `KICK <user>` | Remove a user from room (admin only) |
| `PROMOTE <user>` | Transfer admin role (admin only) |

### Server → Client

| Reply | Meaning |
|---|---|
| `OK <status>` | Command succeeded |
| `ERR <code>` | Command failed with error code |
| `NOTICE <user> <action> <room>` | Presence event (joined, left, kicked, promoted, status change) |
| `MSG <sender> <text>` | Room message |
| `PM_FROM <sender> <text>` | Private message |
| `WHO_REPLY <*name/STATUS> ...` | WHO response (`*` = admin) |
| `ROOMS_REPLY <room> ...` | ROOMS response |

---

## Architecture

```
accept loop (main thread)
      │
      ▼
  threadpool (16 workers)     ← fixed, no per-connection thread creation
      │
      ▼
handle_client(fd)             ← one worker blocked per active connection
      │
      ├── parse_incoming_command_server()
      ├── registry functions (create_room, room_add_member, etc.)
      └── format_*_reply() + send()

Global state (registry.c):
  pthread_rwlock_t registry_lock   — guards room_list[] and client_list[]
  room_t *room_list[16]
  client_t *client_list[64]

Per-room state (room_t):
  pthread_mutex_t room_lock        — guards members[] and history[]
  client_t *members[16]
  message_t history[10]            — circular buffer, replayed on JOIN
```

### Locking rules

- **registry_lock (rwlock):** read-lock for `find_room` / `find_client`. Write-lock for `create_room` / `delete_room` / `create_client` / `delete_client`.
- **room->room_lock (mutex):** held for any access to `room->members[]`, `room->history[]`, `room->member_count`, `room->history_count`.
- **Never hold both locks at the same time** to avoid deadlock.

---

## What Is Already Implemented

| Feature | Where |
|---|---|
| All 11 client commands | `src/client_handler.c` |
| Concurrent multi-client (thread pool, 16 workers) | `src/server.c`, `src/threadpool.c` |
| Per-room mutex + global rwlock | `src/registry.c`, `include/registry.h` |
| Message history (last 10, replayed on JOIN) | `src/registry.c` — `room_add_history`, `room_send_history` |
| Presence status (ONLINE/AWAY/BUSY) | `src/client_handler.c` — `TYPE_STATUS` |
| Admin commands (KICK, PROMOTE) | `src/client_handler.c` — `TYPE_KICK`, `TYPE_PROMOTE` |
| Auto-delete empty rooms | `src/registry.c` — `room_delete_if_empty` |
| Token bucket rate limiting (5 tokens, 1/sec refill) | `src/client_handler.c` — `rate_limit_check` |
| Graceful SIGINT shutdown | `src/server.c` — `handle_sigint`, `notify_all_clients` |
| Color-coded client display with timestamps | `src/display.c`, `replies/client.c` |

---

## What to Do Next (Scaling Up)

These are ordered from most impactful to most complex.

### 1. epoll-based I/O (event-driven, high connection counts)

**Problem:** Each of the 16 pool workers is blocked inside `recv()`. If 16 clients are connected but idle, all workers are occupied. A 17th connection queues until one disconnects.

**Solution:** Replace blocking `recv()` with Linux `epoll`. One (or a few) threads handle thousands of sockets via events. Requires:
- Non-blocking sockets (`fcntl(fd, F_SETFL, O_NONBLOCK)`)
- `epoll_create1`, `epoll_ctl`, `epoll_wait` in the server loop
- Per-connection state struct (replaces the local variables in `handle_client`)
- A state machine instead of a blocking while loop per client

This is the largest change and breaks the current `handle_client` structure. Do this after everything else is stable.

### 2. Persistent message history (SQLite or flat file)

**Problem:** History is in-memory. Restarting the server loses all room history.

**Solution:** Write each message to a SQLite database or append-only log file on disk. On `create_room`, load existing history. Schema example:

```sql
CREATE TABLE messages (
    room TEXT, sender TEXT, text TEXT, timestamp INTEGER
);
```

Link with `-lsqlite3`. The `room_add_history` and `room_send_history` functions in `registry.c` are the only places to change.

### 3. Increase pool size dynamically

**Problem:** `THREADPOOL_SIZE 16` is a compile-time constant. Under load, 16 workers may not be enough.

**Solution:** Implement a dynamic pool — track idle workers, spin up new ones when queue depth exceeds a threshold, shrink when idle for too long. The `threadpool_t` struct and `threadpool.c` are the only files to touch.

### 4. TLS encryption (OpenSSL)

**Problem:** All traffic is plaintext. Anyone on the network can read messages with tcpdump.

**Solution:** Wrap the socket with OpenSSL's `SSL_read` / `SSL_write` instead of `recv` / `send`. The only files that call `recv`/`send` directly are `client_handler.c`, `registry.c` (broadcast), and `replies/client.c`. Replace those calls with an `ssl_send`/`ssl_recv` wrapper that is either a raw socket call or an SSL call depending on whether TLS is enabled.

### 5. Config file

**Problem:** `THREADPOOL_SIZE`, `RATE_BUCKET_MAX`, `RATE_REFILL_RATE`, `MAX_CLIENTS`, `MAX_ROOMS`, `DEFAULT_PORT` are all compile-time `#define` constants. Changing them requires a recompile.

**Solution:** Parse a simple `server.conf` at startup (INI or key=value format). Override compile-time defaults with runtime values. No external library needed — a small `config.c` parser is enough.

### 6. User authentication (passwords)

**Problem:** Anyone can `REGISTER` any username. There is no password and no persistence — usernames reset on server restart.

**Solution:** Store a `username → hashed_password` map (flat file or SQLite). On `REGISTER`, check if the name exists; if so require a matching password. Use `crypt()` (POSIX) or a simple SHA-256 (add `-lcrypto` from OpenSSL).

---

## Known Limitations

| Limitation | Notes |
|---|---|
| Thread pool is fixed at 16 | Max 16 simultaneous active clients. Queue builds up beyond that. |
| No TLS | All traffic is plaintext |
| No persistence | All state (users, rooms, history) is lost on restart |
| No authentication | Any client can claim any username on a fresh server |
| Rate limit uses wall clock | `clock_gettime(CLOCK_MONOTONIC)` — accurate, but resets on reconnect |
| `ROOMS` and `WHO` results are not paginated | Works fine under `MAX_ROOMS=16` / `MAX_MEMBERS=16` |
| No reconnect handling | If a client disconnects and reconnects, they must re-register |

---

## Tuning Constants

All in `include/registry.h` and `include/threadpool.h` — no logic changes needed.

| Constant | File | Default | Effect |
|---|---|---|---|
| `THREADPOOL_SIZE` | `threadpool.h` | `16` | Max concurrent active clients |
| `MAX_CLIENTS` | `registry.h` | `64` | Max registered users at once |
| `MAX_ROOMS` | `registry.h` | `16` | Max simultaneous rooms |
| `MAX_MEMBERS` | `registry.h` | `16` | Max users per room |
| `HISTORY_SIZE` | `registry.h` | `10` | Messages replayed on JOIN |
| `RATE_BUCKET_MAX` | `registry.h` | `5` | Burst allowance per client |
| `RATE_REFILL_RATE` | `registry.h` | `1.0` | Tokens/second added back |
| `RATE_MSG_COST` | `registry.h` | `1` | Tokens per MSG or PM |
