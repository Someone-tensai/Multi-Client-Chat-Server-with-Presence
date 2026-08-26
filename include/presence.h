#ifndef PRESENCE_H
#define PRESENCE_H

#include <time.h>
#include <pthread.h>

// ─────────────────────────────────────────────────────────────────────────────
// Presence — Typing Indicators (Phase 7, ephemeral, no DB)
//
// Typing indicators are ephemeral — they don't persist. When a user types,
// we broadcast TYPING <user> to their room. After a debounce period (3s),
// we auto-send STOP_TYPING if the user hasn't sent another TYPING within
// the debounce window.
//
// Rate-limited: max 1 TYPING per 2 seconds per user to avoid flooding.
// ─────────────────────────────────────────────────────────────────────────────

#define TYPING_DEBOUNCE_SEC 3    // auto-stop after 3 seconds of silence
#define TYPING_RATE_LIMIT_SEC 2  // min interval between TYPING broadcasts

// Typing state per user (tracked in-memory, not in DB)
typedef struct typing_state {
    char username[32];
    int  active;            // 1 = currently typing, 0 = not
    time_t last_typed_at;   // timestamp of last TYPING broadcast
    time_t started_at;      // when typing started
} typing_state_t;

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

// Initialize the presence/typing subsystem.
// Returns 0 on success.
int presence_init(void);

// Called when a user sends TYPING in a room.
// Returns 1 if the typing indicator should be broadcast (rate limit passed),
// 0 if it should be suppressed (too soon).
int presence_typing_start(const char *username);

// Called when a user sends STOP_TYPING or sends a MSG (implicit stop).
void presence_typing_stop(const char *username);

// Check if a user is currently typing (for JOIN/WHO status).
// Returns 1 if typing, 0 if not.
int presence_is_typing(const char *username);

// Periodic cleanup of stale typing states (call from server loop).
// Returns number of states cleaned.
int presence_cleanup_stale(void);

#endif
