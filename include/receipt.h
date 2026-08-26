#ifndef RECEIPT_H
#define RECEIPT_H

#include <stddef.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Read Receipts — Phase 6
//
// Tracks which messages have been read by which users.
// READ <message_id> marks a message as read and broadcasts a READ_RECEIPT
// to the room so other clients know the message was seen.
// ─────────────────────────────────────────────────────────────────────────────

#define MAX_READ_RECEIPTS 1024

// Read receipt record
typedef struct read_receipt {
    long long message_id;
    char reader[32];
    time_t    read_at;
} read_receipt_t;

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

// Record that `reader` has read message `msg_id`.
// Returns 0 on success, -1 on error.
int receipt_mark_read(long long msg_id, const char *reader);

// Check if `reader` has read message `msg_id`.
// Returns 1 if read, 0 if not, -1 on error.
int receipt_is_read(long long msg_id, const char *reader);

// Initialize receipt tables (called at startup).
// Returns 0 on success, -1 on error.
int receipt_init(void);

#endif
