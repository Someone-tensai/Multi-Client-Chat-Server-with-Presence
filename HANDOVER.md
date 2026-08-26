# Handover: Multi-Client Chat Server with Presence

This document is for whoever picks this project up next. It covers the current state of the codebase, the architecture, what has been implemented, known limitations, and what to do next to scale it up.

---

## Team Division — Sulav, Srijal, Janak

The six scaling tasks below are split so that each person works on completely separate files and layers. There is no overlap — you will not conflict on the same file at the same time.

### Sulav — Infrastructure & Transport Layer

**Tasks: epoll-based I/O + TLS encryption**

These are both low-level server infrastructure tasks that live entirely in `src/server.c`, `src/threadpool.c`, and the socket call sites in `src/client_handler.c` and `src/registry.c`.

| Task | Files to touch |
|---|---|
| epoll-based I/O | `src/server.c`, `src/threadpool.c`, `include/threadpool.h` |
| TLS (OpenSSL) | `src/server.c`, `replies/client.c`, wrapper around `send`/`recv` in `src/client_handler.c` and `src/registry.c` |

**Branch:** `feature/sulav-infra`

Do epoll first, then TLS on top. epoll changes how connections are accepted and dispatched. TLS wraps the socket calls — once epoll is in, the send/recv sites are clear and TLS slots in without touching logic.

Do **not** touch `src/registry.c` logic, `src/protocol.c`, or anything in `include/registry.h` — those are Srijal and Janak's territory.

---

### Srijal — Data & Persistence Layer

**Tasks: Persistent message history (SQLite) + User authentication**

These are both data storage tasks. Everything lives in `src/registry.c`, a new `src/db.c` / `include/db.h`, and `src/client_handler.c` (only the REGISTER case for auth).

| Task | Files to touch |
|---|---|
| SQLite persistence | New `src/db.c`, new `include/db.h`, `src/registry.c` (`room_add_history`, `room_send_history`, `create_room`) |
| User authentication | `src/db.c` (store hashed passwords), `src/client_handler.c` (TYPE_REGISTER and a new TYPE_LOGIN case), `include/protocol.h` (add `CMD_LOGIN`, `TYPE_LOGIN`) |

**Branch:** `feature/srijal-persistence`

Add SQLite first — schema:
```sql
CREATE TABLE messages (room TEXT, sender TEXT, text TEXT, timestamp INTEGER);
CREATE TABLE users    (username TEXT PRIMARY KEY, password_hash TEXT);
```

Then add auth on top using the same DB connection. The `registry_lock` rwlock already protects `client_list` — you do not need to add new locks, just wrap DB calls in their own mutex inside `db.c`.

Do **not** touch `src/server.c`, `src/threadpool.c`, or `src/display.c` — those are Sulav and Janak's territory.

---

### Janak — Application & Config Layer

**Tasks: Config file + Dynamic thread pool** ✅ DONE

| Task | Status | Files |
|---|---|---|
| Config file (`server.conf`) | ✅ Complete | New `src/config.c`, new `include/config.h`, `src/server.c` |
| Dynamic thread pool | ✅ Complete | `src/threadpool.c`, `include/threadpool.h` |

**Branch:** `feature/janak-config-pool`

**What was implemented:**

1. **Config file parser** — `src/config.c` / `include/config.h`: Parses `server.conf` (key=value format) at startup. Supports all runtime-tunable parameters: port, thread pool size, max clients/rooms/members, history size, rate limiting, TLS cert/key paths, and pool resize settings. Missing keys fall back to compile-time defaults. Accepts an optional command-line argument for the config path (`./server myconfig.conf`).

2. **Dynamic thread pool** — `src/threadpool.c` / `include/threadpool.h`: Pool starts at `thread_pool_size` and auto-scales:
   - **Scale-up:** When `queue_size > thread_count` and below `max_threads`, new workers are spawned on the fly.
   - **Shrink (idle timeout):** Workers that have been idle longer than `pool_shrink_idle_sec` exit, down to `pool_min_threads`.
   - **Shrink (explicit):** `threadpool_maybe_shrink()` called every 5s from the epoll loop; signals excess idle workers to exit when queue is empty.
   - Thread-safe: all resize operations happen under the pool mutex; worker join via `pthread_join` in destroy is safe with a snapshot of `thread_count`.

---

### Summary Table

| Person | Branch | Files owned | Tasks | Status |
|---|---|---|---|---|
| **Sulav** | `feature/sulav-infra` | `src/server.c`, `src/threadpool.c`, TLS wrappers | epoll, TLS | ✅ Done |
| **Srijal** | `feature/srijal-persistence` | `src/registry.c`, `src/db.c`, `include/db.h` | SQLite history, auth | ✅ Done |
| **Janak** | `feature/janak-config-pool` | `src/config.c`, `include/config.h`, `src/threadpool.c` | Config file, dynamic pool | ✅ Done |

---

### Integration Order

All three feature branches are now complete. The merge order:

1. **Janak merges first** — config and pool changes are foundational; rebase `feature/janak-config-pool` onto `main`, open PR
2. **Srijal merges second** — persistence and auth sit on top of stable registry and config; rebase `feature/srijal-persistence` onto updated `main`, open PR
3. **Sulav merges last** — epoll and TLS are transport-level and wrap everything else; rebase `feature/sulav-infra` onto updated `main`, open PR

> **Note:** `origin/janak-branch` contains only an old Makefile fill-in and a naive per-thread `pthread_create` approach (superseded by the threadpool). The actual config + dynamic pool work was done directly on the current codebase and should be committed to a new branch or force-pushed to `feature/janak-config-pool`.

---


## How to Build and Run

**Requirements:** Linux or WSL (Ubuntu), gcc, make, pthreads (included with glibc)

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

---

## Project Structure

```
include/
  common.h        — shared includes (stdio, socket, netinet, etc.)
  protocol.h      — command/reply string constants, cmd struct, function declarations
  registry.h      — client_t, room_t, message_t structs, all registry API declarations
  server.h        — run_server(), handle_client() declarations
  display.h       — display_*() function declarations
  threadpool.h    — threadpool_t struct and API (dynamic resize)
  config.h        — server_config_t struct, config_load(), config_get()
  db.h            — SQLite persistence API

src/
  server.c        — main(), run_server(), SIGINT handler, config integration
  client_handler.c — per-client command dispatch loop (all commands implemented)
  registry.c      — room and client state: create, find, add, remove, broadcast, delete
  protocol.c      — parse_incoming_command_server(), format_*_reply() functions
  display.c       — color-coded terminal output functions
  threadpool.c    — dynamic worker thread pool (auto-scale + shrink)
  config.c        — key=value config file parser
  db.c            — SQLite persistence (messages, users with SHA-256 auth)

replies/
  client.c        — full client program (connects, sends commands, displays output)

tests/
  test_registry.c — unit tests for registry functions

server.conf       — runtime config file (key=value, loaded at startup)
```

---

## Protocol

Text-based, newline-terminated. All commands are case-sensitive.

### Client → Server

| Command | Description |
|---|---|
| `REGISTER <name> <password>` | Register username with password (must be first command) |
| `LOGIN <name> <password>` | Login with existing credentials |
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
config_load("server.conf")     ← parses key=value, populates server_config_t
      │
      ▼
accept loop (main thread)      ← epoll_wait, non-blocking I/O
      │
      ▼
  threadpool (dynamic)         ← scales up on load, shrinks on idle
      │                           configured via server.conf
      ▼
handle_client(conn)            ← one worker per burst of data
      │                           transparent TLS via conn_send/conn_recv
      ├── parse_incoming_command_server()
      ├── registry functions (create_room, room_add_member, etc.)
      ├── db_* functions (SQLite persistence, user auth)
      └── format_*_reply() + conn_send()

Global state (registry.c):
  pthread_rwlock_t registry_lock   — guards room_list[] and client_list[]
  room_t *room_list[MAX_ROOMS]
  client_t *client_list[MAX_CLIENTS]

Per-room state (room_t):
  pthread_mutex_t room_lock        — guards members[] and history[]
  client_t *members[MAX_MEMBERS]
  message_t history[HISTORY_SIZE]  — circular buffer, replayed on JOIN

Database (db.c → chat.db, SQLite WAL mode):
  messages table — persisted room message history
  users table    — username + SHA-256 password hash
```

### Locking rules

- **registry_lock (rwlock):** read-lock for `find_room` / `find_client`. Write-lock for `create_room` / `delete_room` / `create_client` / `delete_client`.
- **room->room_lock (mutex):** held for any access to `room->members[]`, `room->history[]`, `room->member_count`, `room->history_count`.
- **Never hold both locks at the same time** to avoid deadlock.

---

## What Is Already Implemented

| Feature | Where |
|---|---|
| All 12 client commands (REGISTER, LOGIN, CREATE, JOIN, LEAVE, MSG, PM, WHO, ROOMS, STATUS, KICK, PROMOTE) | `src/client_handler.c` |
| epoll-based I/O (non-blocking, EPOLLONESHOT) | `src/server.c` |
| TLS encryption (optional, OpenSSL) | `src/server.c` (`tls_init`, `conn_send`/`conn_recv` wrappers) |
| Dynamic thread pool (auto-scale up, shrink on idle) | `src/threadpool.c`, `include/threadpool.h` |
| Runtime config file (`server.conf`, key=value) | `src/config.c`, `include/config.h` |
| SQLite message history (persistent, loaded on JOIN) | `src/db.c` — `db_save_message`, `db_load_history` |
| User authentication (REGISTER + LOGIN, SHA-256 passwords) | `src/db.c` — `db_register_user`, `db_verify_user` |
| Per-room mutex + global rwlock | `src/registry.c`, `include/registry.h` |
| Presence status (ONLINE/AWAY/BUSY) | `src/client_handler.c` — `TYPE_STATUS` |
| Admin commands (KICK, PROMOTE) | `src/client_handler.c` — `TYPE_KICK`, `TYPE_PROMOTE` |
| Auto-delete empty rooms | `src/registry.c` — `room_delete_if_empty` |
| Token bucket rate limiting (configurable) | `src/client_handler.c` — `rate_limit_check` |
| Graceful SIGINT shutdown | `src/server.c` — `handle_sigint`, `notify_all_clients` |
| Color-coded client display with timestamps | `src/display.c`, `replies/client.c` |

---

## What to Do Next

All six original scaling tasks are complete. Future work:

### 1. Dynamic room/client array sizing

**Problem:** `MAX_CLIENTS`, `MAX_ROOMS`, `MAX_MEMBERS`, `HISTORY_SIZE` are still compile-time constants used for fixed arrays in `registry.h`. Config loads them at runtime, but the arrays don't resize.

**Solution:** Convert `room_list[]`, `client_list[]`, `room->members[]`, `room->history[]` to dynamically allocated arrays sized from `server_config_t` at startup. Requires careful lock coordination.

### 2. Client reconnect handling

**Problem:** If a client disconnects and reconnects, they must re-register. There is no session persistence.

**Solution:** Store session tokens in the DB or use a reconnection protocol (client sends a session ID on reconnect).

### 3. Message pagination

**Problem:** `ROOMS` and `WHO` results are not paginated. Works under current limits but will break with larger pools.

**Solution:** Add offset/limit parameters to `WHO` and `ROOMS` commands, or batch large responses.

### 4. Logging framework

**Problem:** `printf`/`perror` scattered throughout. No log levels, no file output.

**Solution:** Add a simple `log.h` with `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` macros. Output to stdout + optional log file.

---

## Known Limitations

| Limitation | Notes |
|---|---|
| Fixed array sizes | `MAX_CLIENTS`, `MAX_ROOMS`, etc. are compile-time; config loads values but arrays don't resize |
| No reconnect handling | If a client disconnects and reconnects, they must re-register |
| `ROOMS` and `WHO` results not paginated | Fine under current limits, will need paging at scale |
| No logging framework | `printf`/`perror` only — no log levels, no file output |

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
