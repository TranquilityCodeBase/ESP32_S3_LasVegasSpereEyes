// eye_sync.h - ESP-NOW link between the two eyeball boards (stub, off by default).
// The leader publishes where it looks and when it blinks. The follower copies it, so both faces move together.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "gaze_source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EYE_SYNC_ROLE_OFF = 0,      // No radio. All calls are no-ops
    EYE_SYNC_ROLE_LEADER,       // Sends its state
    EYE_SYNC_ROLE_FOLLOWER,     // Receives and copies the leader's state
} eye_sync_role_t;

// State shared over the air. Keep it small and versioned.
typedef struct {
    float gazeX;            // Leader's gaze target x, -1..1
    float gazeY;            // Leader's gaze target y, -1..1
    uint32_t blinkSeq;      // Increments on every blink the leader starts
    uint8_t blinkIndex;     // Index into blinkEffectTable of that blink
    bool hasTarget;         // true = leader is tracking a person, false = idle wander
    uint8_t styleIndex;     // Which design the leader shows (index into the style playlist in app_main.c)
} eye_sync_state_t;

// Link settings. Secrets come from main/secrets.h (gitignored), never from committed code.
typedef struct {
    eye_sync_role_t role;       // Leader, follower or off
    uint8_t channel;            // Wi-Fi channel both boards use, 1..13
    uint8_t groupId;            // Messages with another group id are ignored (several pairs in one room)
    const uint8_t *peerMac;     // 6 bytes, or NULL = broadcast (no MAC needed)
    const uint8_t *pmk;         // 16-byte ESP-NOW primary key, or NULL = unencrypted
    const uint8_t *lmk;         // 16-byte local key for the peer, used only with peerMac and pmk
} eye_sync_config_t;

esp_err_t eyeSyncInit(const eye_sync_config_t *config);      // Start Wi-Fi (station, no connect) and ESP-NOW
void eyeSyncPublish(const eye_sync_state_t *state);          // Leader: send state (rate limited inside)
bool eyeSyncGetLatest(eye_sync_state_t *out, uint32_t maxAgeMs);   // Follower: latest state if fresher than maxAgeMs

// Gaze source that follows the leader's gaze. Valid only on a follower with a fresh message.
extern const gaze_source_t gazeSourceSync;

#ifdef __cplusplus
}
#endif
