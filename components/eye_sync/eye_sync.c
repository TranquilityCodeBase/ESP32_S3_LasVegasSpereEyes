// eye_sync.c - ESP-NOW link that lets a follower eye copy the leader's gaze and blinks.
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_now.h"
#include "eye_sync.h"

#define SYNC_MAGIC0        'E'      // Packet magic bytes
#define SYNC_MAGIC1        'Y'
#define SYNC_VERSION       1        // Bump when the packet layout changes
#define SYNC_SCALE         10000.0f // Floats travel as int16 x10000
#define SYNC_MIN_GAP_US    50000    // 20 Hz publish limit
#define SYNC_FRESH_MS      300      // Follower gaze source freshness window

// Over-the-air packet
typedef struct __attribute__((packed)) {
    uint8_t magic[2];       // 'E','Y'
    uint8_t version;        // SYNC_VERSION
    uint8_t groupId;        // Pair id
    uint16_t sequence;      // Packet counter
    int16_t gazeX;          // x10000
    int16_t gazeY;          // x10000
    uint32_t blinkSeq;      // Leader blink counter
    uint8_t blinkIndex;     // Blink effect index
    uint8_t hasTarget;      // 0/1
} eye_sync_packet_t;

static const char *TAG = "eye_sync";
static const uint8_t syncBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // Used when no peerMac

static eye_sync_role_t syncRole = EYE_SYNC_ROLE_OFF;    // Active role
static uint8_t syncGroupId = 0;                         // Active pair id
static uint8_t syncPeer[6];                             // Send destination
static bool syncReady = false;                          // true once ESP-NOW is up
static eye_sync_state_t syncLatest;                     // Last received state
static int64_t syncStampUs = 0;                         // Receive time, 0 = none yet
static portMUX_TYPE syncMux = portMUX_INITIALIZER_UNLOCKED;   // Guards latest + stamp
static uint16_t syncSeq = 0;                            // Outgoing packet counter
static int64_t syncLastSendUs = 0;                      // Last publish time
static uint32_t syncLastBlinkSeq = 0;                   // Blink counter at last publish

static int16_t syncPack(float v) {                      // Float -1..1 to scaled int16
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v * SYNC_SCALE);
}

// ESP-NOW receive callback (Wi-Fi task): validate and store
static void syncOnRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    (void)info;
    if (data == NULL || len != (int)sizeof(eye_sync_packet_t)) return;   // Wrong size
    const eye_sync_packet_t *pkt = (const eye_sync_packet_t *)data;
    if (pkt->magic[0] != SYNC_MAGIC0 || pkt->magic[1] != SYNC_MAGIC1) return;   // Not ours
    if (pkt->version != SYNC_VERSION || pkt->groupId != syncGroupId) return;    // Other version/pair
    eye_sync_state_t s = {
        .gazeX = pkt->gazeX / SYNC_SCALE,
        .gazeY = pkt->gazeY / SYNC_SCALE,
        .blinkSeq = pkt->blinkSeq,
        .blinkIndex = pkt->blinkIndex,
        .hasTarget = pkt->hasTarget != 0,
    };
    int64_t now = esp_timer_get_time();                 // Stamp outside the lock
    portENTER_CRITICAL(&syncMux);
    syncLatest = s;
    syncStampUs = now;
    portEXIT_CRITICAL(&syncMux);
}

esp_err_t eyeSyncInit(const eye_sync_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (config->role == EYE_SYNC_ROLE_OFF) return ESP_OK;   // Radio stays off
    if (syncReady) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_netif_init();                   // Main already ran nvs_flash_init
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    wifi_init_config_t wifiCfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&wifiCfg)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;   // No flash writes
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;         // Station, never connects
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    if ((err = esp_wifi_set_channel(config->channel, WIFI_SECOND_CHAN_NONE)) != ESP_OK) return err;
    if ((err = esp_now_init()) != ESP_OK) return err;
    if (config->pmk != NULL && (err = esp_now_set_pmk(config->pmk)) != ESP_OK) return err;   // Optional encryption key
    bool encrypt = config->peerMac != NULL && config->lmk != NULL && config->pmk != NULL;   // Encrypt only unicast with both keys
    esp_now_peer_info_t peer = {0};
    memcpy(syncPeer, config->peerMac ? config->peerMac : syncBroadcastMac, 6);   // Unicast or broadcast
    memcpy(peer.peer_addr, syncPeer, 6);
    peer.channel = 0;                                   // 0 = current channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = encrypt;
    if (encrypt) memcpy(peer.lmk, config->lmk, 16);
    if ((err = esp_now_add_peer(&peer)) != ESP_OK) return err;
    syncRole = config->role;
    syncGroupId = config->groupId;
    if (syncRole == EYE_SYNC_ROLE_FOLLOWER) {
        if ((err = esp_now_register_recv_cb(syncOnRecv)) != ESP_OK) return err;   // Followers listen
    }
    syncReady = true;
    ESP_LOGI(TAG, "Role %d, channel %d, group %d, %s", (int)syncRole, config->channel, syncGroupId, encrypt ? "encrypted" : "open");
    return ESP_OK;
}

void eyeSyncPublish(const eye_sync_state_t *state) {
    if (!syncReady || syncRole != EYE_SYNC_ROLE_LEADER || state == NULL) return;   // Leader only
    int64_t now = esp_timer_get_time();
    bool blinked = state->blinkSeq != syncLastBlinkSeq; // Blinks bypass the rate limit
    if (!blinked && now - syncLastSendUs < SYNC_MIN_GAP_US) return;   // Max 20 Hz otherwise
    eye_sync_packet_t pkt = {
        .magic = {SYNC_MAGIC0, SYNC_MAGIC1},
        .version = SYNC_VERSION,
        .groupId = syncGroupId,
        .sequence = syncSeq++,
        .gazeX = syncPack(state->gazeX),
        .gazeY = syncPack(state->gazeY),
        .blinkSeq = state->blinkSeq,
        .blinkIndex = state->blinkIndex,
        .hasTarget = state->hasTarget ? 1 : 0,
    };
    esp_err_t err = esp_now_send(syncPeer, (const uint8_t *)&pkt, sizeof(pkt));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(err));   // Try again on the next call
        return;
    }
    syncLastSendUs = now;
    syncLastBlinkSeq = state->blinkSeq;
}

bool eyeSyncGetLatest(eye_sync_state_t *out, uint32_t maxAgeMs) {
    if (!syncReady || out == NULL) return false;        // Off or not started
    eye_sync_state_t s;
    int64_t stamp;
    portENTER_CRITICAL(&syncMux);
    s = syncLatest;
    stamp = syncStampUs;
    portEXIT_CRITICAL(&syncMux);
    if (stamp == 0) return false;                       // Nothing received yet
    if ((esp_timer_get_time() - stamp) / 1000 > (int64_t)maxAgeMs) return false;   // Too old
    *out = s;
    return true;
}

// Gaze source poll: copy the leader's gaze (including idle wander) while fresh
static bool syncPoll(uint32_t nowMs, gaze_target_t *out) {
    (void)nowMs;
    eye_sync_state_t s;
    if (syncRole != EYE_SYNC_ROLE_FOLLOWER || out == NULL) return false;
    if (!eyeSyncGetLatest(&s, SYNC_FRESH_MS)) return false;
    out->x = s.gazeX;
    out->y = s.gazeY;
    out->distanceM = 0.0f;                              // Not sent
    return true;
}

const gaze_source_t gazeSourceSync = {
    .name = "sync",
    .init = NULL,                                       // main calls eyeSyncInit with its config
    .poll = syncPoll,
};
