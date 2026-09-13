#pragma once

// Copy to include/secrets.h (gitignored). Generate PSK via: openssl rand -hex 16

// WiFi / OTA Configuration
#define SECRET_WIFI_SSID     ""
#define SECRET_WIFI_PASS     ""
#define SECRET_OTA_PASSWORD  ""

// Link Encryption (32 hex characters / 16 bytes for AES-128)
#define SECRET_PSK_HEX       ""

// Device Addressing (Node ID range: 1-255)
#define SECRET_THIS_DEVICE_ID 1
#define SECRET_PEER_DEVICE_ID 2
