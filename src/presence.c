#include "../include/presence.h"
#include "../include/log.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

// ─────────────────────────────────────────────────────────────────────────────
// Presence — Typing Indicators (ephemeral, no DB)
//
// In-memory tracking of who is currently typing in which room.
// Rate-limited to prevent flooding. Debounce auto-stops after silence.
// ─────────────────────────────────────────────────────────────────────────────

#define MAX_TYPING_USERS 256

static typing_state_t typing_states[MAX_TYPING_USERS];
static int typing_count = 0;
static pthread_mutex_t typing_lock = PTHREAD_MUTEX_INITIALIZER;

// ─────────────────────────────────────────────────────────────────────────────
// Find or create typing state for a user
// ─────────────────────────────────────────────────────────────────────────────
static typing_state_t *find_typing_state(const char *username)
{
    for (int i = 0; i < typing_count; i++)
    {
        if (strcmp(typing_states[i].username, username) == 0)
            return &typing_states[i];
    }

    // Create new entry if space available
    if (typing_count < MAX_TYPING_USERS)
    {
        typing_state_t *s = &typing_states[typing_count++];
        strncpy(s->username, username, sizeof(s->username) - 1);
        s->username[sizeof(s->username) - 1] = '\0';
        s->active = 0;
        s->last_typed_at = 0;
        s->started_at = 0;
        return s;
    }

    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize presence subsystem
// ─────────────────────────────────────────────────────────────────────────────
int presence_init(void)
{
    pthread_mutex_lock(&typing_lock);
    typing_count = 0;
    pthread_mutex_unlock(&typing_lock);
    LOG_INFO("Presence subsystem initialized (typing indicators enabled)");
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// User started typing — returns 1 if broadcast should happen
// ─────────────────────────────────────────────────────────────────────────────
int presence_typing_start(const char *username)
{
    if (!username) return 0;

    time_t now = time(NULL);
    int should_broadcast = 0;

    pthread_mutex_lock(&typing_lock);

    typing_state_t *state = find_typing_state(username);
    if (state)
    {
        // Rate limit: only broadcast if enough time since last TYPING
        if (!state->active ||
            (now - state->last_typed_at) >= TYPING_RATE_LIMIT_SEC)
        {
            state->active = 1;
            state->last_typed_at = now;
            if (state->started_at == 0)
                state->started_at = now;
            should_broadcast = 1;
        }
        // else: rate-limited, suppress broadcast
    }

    pthread_mutex_unlock(&typing_lock);
    return should_broadcast;
}

// ─────────────────────────────────────────────────────────────────────────────
// User stopped typing (explicit or implicit via MSG)
// ─────────────────────────────────────────────────────────────────────────────
void presence_typing_stop(const char *username)
{
    if (!username) return;

    pthread_mutex_lock(&typing_lock);

    typing_state_t *state = find_typing_state(username);
    if (state)
    {
        state->active = 0;
        state->started_at = 0;
    }

    pthread_mutex_unlock(&typing_lock);
}

// ─────────────────────────────────────────────────────────────────────────────
// Check if user is currently typing
// ─────────────────────────────────────────────────────────────────────────────
int presence_is_typing(const char *username)
{
    if (!username) return 0;

    pthread_mutex_lock(&typing_lock);

    typing_state_t *state = find_typing_state(username);
    int result = (state && state->active) ? 1 : 0;

    pthread_mutex_unlock(&typing_lock);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cleanup stale typing states (auto-stop after debounce period)
// Returns number of states auto-stopped
// ─────────────────────────────────────────────────────────────────────────────
int presence_cleanup_stale(void)
{
    time_t now = time(NULL);
    int cleaned = 0;

    pthread_mutex_lock(&typing_lock);

    for (int i = 0; i < typing_count; i++)
    {
        if (typing_states[i].active &&
            (now - typing_states[i].started_at) >= TYPING_DEBOUNCE_SEC)
        {
            typing_states[i].active = 0;
            typing_states[i].started_at = 0;
            cleaned++;
            LOG_DEBUG("Auto-stopped typing for %s (debounce)", typing_states[i].username);
        }
    }

    pthread_mutex_unlock(&typing_lock);
    return cleaned;
}
