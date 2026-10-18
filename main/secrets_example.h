// secrets_example.h - Template for main/secrets.h, which is gitignored. Copy it and fill in real values there.
// This repo is public: never put keys or MAC addresses in a committed file.
#pragma once

// ESP-NOW encryption. 0 = unencrypted broadcast (fine for a demo). 1 = use the keys below with the peer MAC.
#define SECRET_SYNC_USE_KEYS    0
#define SECRET_SYNC_PMK         {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define SECRET_SYNC_LMK         {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

// Peer board's station MAC (printed at boot). 0 = broadcast, no MAC needed.
#define SECRET_SYNC_USE_PEER_MAC    0
#define SECRET_SYNC_PEER_MAC    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
