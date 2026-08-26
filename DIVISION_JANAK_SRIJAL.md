# Scale-Up Division — Janak & Srijal (2-Person, Zero-File-Conflict)

This document divides the 30+ phase `Scale-Up Roadmap` between **Janak** and **Srijal** for parallel work with **no file overlap**. Each person owns completely separate files and layers. You will not edit the same file at the same time.

> Base: `v1.0-complete` (or current `main` after dynamic arrays / sessions / pagination / logging are merged). Branch from `main` and keep rebase discipline per Integration Order below.

---

## Ownership Principle

- A file listed under **Janak** must **not** be edited by Srijal in his branch, and vice versa.
- New features must be implemented in **new files owned by that person**. Shared dispatcher `src/server.c` is owned by **Srijal only** (Janak never touches it after Phase 0). Shared `Makefile` is owned by **Janak only** — Srijal documents his new `SRCS` additions in his README and Janak merges them at integration.
- Never hold `registry_lock` and `room_lock` at the same time (existing rule). Never hold `db_lock` together with either.
- All DB writes go through the owner of `src/db.c` (Janak). Srijal accesses persistence via Janak's `db_*` abstraction, never raw SQL outside his own new store files.

---

## File Ownership Matrix

| Path | Owner | Notes |
|---|---|---|
| `include/registry.h` | **Janak** | dynamic `room_list/client_list/members/history`, `registry_init` |
| `src/registry.c` | **Janak** | allocation, `room_destroy`, history circular buffer |
| `include/protocol.h` | **Janak** | `CMD_*`, `TYPE_*`, `REPLY_*`, pagination structs |
| `src/protocol.c` | **Janak** | parser, `format_*` |
| `include/config.h` | **Janak** | `server_config_t`, defaults |
| `src/config.c` | **Janak** | key=value parser |
| `include/log.h` | **Janak** | logger API |
| `src/log.c` | **Janak** | logger impl |
| `include/db.h` | **Janak** | DB abstraction (SQLite/PostgreSQL) |
| `src/db.c` | **Janak** | `users/messages` + future `rooms/room_members` persistence |
| `include/message.h` **NEW** | **Janak** | `message_id`, `message_t` v2 |
| `src/message.c` **NEW** | **Janak** | `message_create/edit/delete`, delivery semantics |
| `include/history.h` **NEW** | **Janak** | cursor pagination for `HISTORY` |
| `src/history.c` **NEW** | **Janak** | `HISTORY` command impl, FTS/search |
| `replies/client.c` | **Janak** | CLI improvements, `/help`, history/reconnect UI |
| `Makefile` | **Janak** | owns `SERVER_SRCS` list (Srijal lists his additions in PR description) |
| `server.conf` | **Janak** | owns defaults (Srijal proposes additions via PR comment) |
| `include/server.h` | **Srijal** | `run_server` signature, health/ready |
| `src/server.c` | **Srijal** | accept/epoll, SIGTERM graceful, TLS hardening, `/health`/`/ready`, graceful deployment |
| `include/conn.h` | **Srijal** | `conn_t` |
| `include/threadpool.h` | **Srijal** | pool API |
| `src/threadpool.c` | **Srijal** | pool (already dynamic) + observability counters |
| `src/client_handler.c` | **Srijal** | dispatcher — owns `REGISTER/LOGIN/RECONNECT/LOGOUT`, `TYPING`, `BLOCK`, `INVITE`, `SESSIONS` dispatch (calls Janak's `message.c`/`history.c` via thin `message_handle_*` helpers — does not edit Janak's logic) |
| `include/session.h` **NEW** | **Srijal** | session lifecycle |
| `src/session.c` **NEW** | **Srijal** | `session_create/validate/revoke/refresh/cleanup` (secure `RAND_bytes`) |
| `include/presence.h` **NEW** | **Srijal** | presence + Redis |
| `src/presence.c` **NEW** | **Srijal** | `TYPING/STOP_TYPING`, `ONLINE/OFFLINE` via Redis |
| `include/permission.h` **NEW** | **Srijal** | roles `OWNER/ADMIN/MODERATOR/MEMBER` |
| `src/permission.c` **NEW** | **Srijal** | `KICK/BAN/DEMOTE/MUTE`, `permission_check` |
| `include/redis.h` **NEW** | **Srijal** | Redis abstraction |
| `src/redis.c` **NEW** | **Srijal** | pub/sub, presence, rate-limit Lua |
| `include/pg.h` **NEW** | **Srijal** | PostgreSQL abstraction |
| `src/pg.c` **NEW** | **Srijal** | PG impl behind `db_*` interface |
| `Dockerfile.server` **NEW** | **Srijal** | |
| `docker-compose.yml` **NEW** | **Srijal** | server + postgres + redis + prometheus/grafana |
| `include/metrics.h` **NEW** | **Srijal** | |
| `src/metrics.c` **NEW** | **Srijal** | Prometheus counters |

**Never cross-edit:** Janak never touches `src/server.c`, `src/threadpool.c`, `src/session.c`, `src/redis.c`, `src/presence.c`, `src/permission.c`. Srijal never touches `src/registry.c`, `src/protocol.c`, `src/db.c`, `src/log.c`, `src/message.c`, `src/history.c`.

---

## Janak — Storage & Messaging Core

**Branch:** `feature/janak-scale`

**Owns:** all **Janak** files above. Do not touch Srijal's files.

### Milestone 1 — Better Chat (Janak leads)

| Phase | Task | Files to create/touch (Janak only) |
|---|---|---|
| 1 | **Complete Runtime Dynamic Storage** (if not already merged) | `include/registry.h`, `src/registry.c`, `include/config.h`, `src/config.c` |
| 3 | **Message IDs & Delivery Semantics** | NEW `include/message.h`, `src/message.c`, `include/protocol.h` (add `message_id` to replies), `src/db.c` (add `messages.message_id` PK, `message_id` gen) |
| 4 | **Message History Pagination** (extend `WHO/ROOMS` pagination already done → add `HISTORY`) | NEW `include/history.h`, `src/history.c`, `src/db.c` (cursor query `HISTORY room cursor limit`), `src/protocol.c` (parse `HISTORY room cursor limit`, format `HISTORY_REPLY`) |
| 5 | **Message Editing and Deletion** | `src/message.c` (`EDIT/DELETE`), `src/protocol.c` (`CMD_EDIT/DELETE`), `src/db.c` (`messages.edited_at/deleted_at`) |
| 12 | **Direct Message Improvements** (`DM_HISTORY`) | `src/db.c` (`direct_messages` table), `src/history.c` (reuse cursor), `include/protocol.h` (`DM_HISTORY`) |
| 14 | **@Mentions** | `src/message.c` (detect `@user`), `src/protocol.c` (`MENTION`) |
| 15 | **Search** | `src/db.c` + `src/history.c` (SQLite FTS5 `messages_fts`, `SEARCH room query limit`) |

### Milestone 2 — Production Essentials (Janak)

| Phase | Task | Files |
|---|---|---|
| 10 | **Persistent Rooms** (`rooms`, `room_members` tables) | `src/db.c` (add `rooms` + `room_members`), `src/registry.c` (load from DB on `registry_init`) |
| 27 | **Structured Logging** (extend) | `include/log.h`, `src/log.c` (add `server_id`, `connection_id`, JSON option, rotation) |
| 30-31 | **Client Improvements** (history, reconnect, block UI) | `replies/client.c` (`/history`, `/search`, unread, typing display) |

**Order:** 1 → 3 → 4 → 5 → 12 → 14 → 15 → 10 → 27 → 31. Each builds on message IDs.

**Do NOT touch:** `src/server.c`, `src/threadpool.c`, `src/session.c`, `src/redis.c`, `src/presence.c`, `src/permission.c` — those are Srijal's.

---

## Srijal — Session, Security & Distributed Layer

**Branch:** `feature/srijal-scale`

**Owns:** all **Srijal** files above. Do not touch Janak's files.

### Milestone 1 — Session & Realtime

| Phase | Task | Files to create/touch (Srijal only) |
|---|---|---|
| 2 | **Proper Session Management** (multi-connection `User→Session→Connection`) | NEW `include/session.h`, `src/session.c` (`session_create/validate/revoke/refresh/cleanup_expired` with `RAND_bytes`, store `token_hash`), `src/client_handler.c` (add `RECONNECT`/`LOGOUT` dispatch, `SESSIONS`/`REVOKE_SESSION`/`LOGOUT_ALL`), `src/redis.c` (optional session location cache) |
| 6 | **Read Receipts** | NEW `src/session.c` (`READ <message_id>` → `READ_RECEIPT`), `include/protocol.h` add `READ` only via Srijal's `protocol` addition? **Avoid conflict:** Janak owns `protocol.h` — Srijal adds `READ` in `include/session.h` and includes it; Janak merges `protocol.h` change at integration. Or Srijal adds `include/receipt.h`. **Chosen:** Srijal creates `include/receipt.h` + `src/receipt.c` for `READ_RECEIPT` table. |
| 7 | **Typing Indicators** (ephemeral, no DB) | `src/presence.c` (`TYPING/STOP_TYPING` with debounce, broadcast via `room_broadcast` helper, rate-limited), `src/redis.c` pub/sub for cross-node typing |

### Milestone 2 — Moderation & Security

| Phase | Task | Files |
|---|---|---|
| 8 | **User Blocking and Muting** | NEW `src/block.c` (`include/block.h` with `blocks` table) — Srijal creates this, Janak's `db.c` is **not** touched; Srijal's `block.c` uses its own SQLite handle or calls Janak's `db_exec` via callback registered at startup to avoid `db.c` conflict |
| 9 | **Room Permission System** (`OWNER/ADMIN/MODERATOR/MEMBER`) | `src/permission.c` (`permission_check` for `KICK/BAN/MUTE/DEMOTE`), `src/client_handler.c` (enforce via `permission_check`) |
| 11 | **Private Rooms and Invitations** | `src/permission.c` + NEW `src/invite.c` (`CREATE_PRIVATE/INVITE/ACCEPT/DECLINE`, `room.is_private`) |
| 17-18 | **Better Auth (Argon2id) + Security Hardening** | `src/session.c` (migrate SHA-256→Argon2id on login, rehash), `src/server.c` (login rate limiting, lockout, `SESSIONS` listing), `src/server.c` TLS hardening (min version, ciphers) |

### Milestone 3 — Distributed

| Phase | Task | Files |
|---|---|---|
| 19-22 | **Redis for Presence, Pub/Sub, Rate Limiting, User Connection Registry** | `include/redis.h`, `src/redis.c` (presence `user:{u}:connections`, `publish user:{u}`, Lua token bucket), `src/presence.c`, `src/server.c` (replace local `registry_lock` presence with Redis), `src/threadpool.c` (metrics) |
| 23-24 | **PostgreSQL Migration + Indexing** | `include/pg.h`, `src/pg.c` (PG impl of `db_*` interface), `src/db.c` stays as SQLite fallback — Srijal adds `src/pg.c` but **does not edit** `src/db.c`; Janak's `db.c` remains SQLite, Srijal's `pg.c` is selected via `config.db_backend` |
| 25-26,28-29 | **Observability, Health Checks, Docker, Horizontal Scaling** | `src/metrics.c`/`metrics.h`, `src/server.c` (`/health`/`/ready`), `Dockerfile.server`, `docker-compose.yml` |
| 34 | **Graceful Deployment** (`SIGTERM` → drain → notify → flush) | `src/server.c` |

**Order:** 2 → 6 → 7 → 8 → 9 → 11 → 17 → 18 → 19 → 20 → 21 → 22 → 23 → 25 → 26 → 28 → 29 → 34. Each builds on session/presence.

**Do NOT touch:** `src/registry.c`, `src/protocol.c`, `src/db.c`, `src/log.c`, `src/message.c`, `src/history.c`, `replies/client.c` — those are Janak's.

---

## How to Avoid Makefile / server.conf Conflicts

- **Makefile:** Only Janak edits `Makefile`. Srijal's new files (`src/session.c`, `src/redis.c`, etc.) are listed in `Makefile.srijal.inc` (Srijal creates this file, not `Makefile`). At integration, Janak runs `cat Makefile.srijal.inc >> Makefile` and commits.
- **server.conf:** Only Janak edits `server.conf`. Srijal's new keys (`redis_url`, `pg_dsn`, `session_expire`, `tls_min_version`) are documented in `server.conf.srijal.example` (Srijal creates). Janak merges keys at integration.
- **protocol.h:** To avoid both editing `include/protocol.h` at once, Janak owns it (adds `HISTORY/EDIT/DELETE/SEARCH`), Srijal adds `READ/TYPING/BLOCK/INVITE` via new headers `include/receipt.h`, `include/presence.h`, `include/block.h`, `include/invite.h` and includes them in `client_handler.c`. At integration, Srijal's new commands are moved into `protocol.h` by Janak in one merge commit.

---

## Integration Order

1. **Janak merges first** — storage and message IDs are foundational. Rebase `feature/janak-scale` onto `main`, PR, CI (`make`, `/tmp/test_registry`).
2. **Srijal merges second** — session/distributed layer depends on stable `registry` + `protocol`. Rebase `feature/srijal-scale` onto updated `main` (resolving `Makefile`/`protocol.h` via the `.inc` files above), PR.
3. No parallel edits to same file → no rebase conflicts on owned files. Only `Makefile`/`server.conf` are merged via the `.inc` pattern.

---

## Testing Responsibilities

| Person | Tests |
|---|---|
| Janak | `tests/test_registry.c` (dynamic, wrap, zero), `tests/test_message.c` NEW (message_id, edit/delete, history cursor), `tests/test_search.c` NEW (FTS), integration `HISTORY` pagination |
| Srijal | `tests/test_session.c` NEW (create/validate/rotate/expire, not rand), `tests/test_presence.c` NEW (typing debounce), `tests/test_permission.c` NEW (roles, ban), Redis pub/sub distributed test (`Client A→Server A→Redis→Server B→Client B`), `tests/load_test.py` (100/500 conns) |

Both run `make clean && make && /tmp/test_registry && python3 tests/integration_test.py` before PR.

---

## What NOT to Do Together

- Do not both edit `src/db.c` — Srijal's `src/session.c`/`src/block.c` use Janak's `db_*` API or their own store files.
- Do not both edit `src/client_handler.c` logic for same command — Janak handles `MSG/EDIT/DELETE/HISTORY` via `src/message.c` helpers, Srijal handles `RECONNECT/TYPING/BLOCK` via `src/session.c`/`src/presence.c` helpers. Dispatcher `src/client_handler.c` is Srijal's, Janak only adds `extern` calls in his PR description for Srijal to wire at integration.
- Do not add Redis/PostgreSQL dependencies to Janak's `Makefile` entries — Srijal's `Makefile.srijal.inc` adds `-lhiredis -lpq`.

---

## Definition of Done for This Division

- Janak's branch passes `make -Wall -Wextra -Wpedantic` and his `tests/test_*` with no `MAX_*` usage.
- Srijal's branch passes same and `RAND_bytes` used, no token in logs, `RECONNECT` survives restart.
- After both merges, `make clean && make` + `tests/integration_test.py` + `python3 tests/load_test.py` show no deadlocks, no silent message loss, presence correct after Redis kill/restart.
