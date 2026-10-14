// Copy to codex_notify_private.h (git-ignored) and fill in your own values.
// Without codex_notify_private.h the firmware builds without notifications and recording.
// The device ID is the MQTT username; it must match the broker ACL and the server's
// recording.json "devices" list.
#pragma once
#define CODEX_NOTIFY_URL "wss://notify.example.com/codex-notify/mqtt"
#define CODEX_NOTIFY_USERNAME "passport-01"
#define CODEX_NOTIFY_PASSWORD "<device password from config/notify.local.json>"
#define CODEX_NOTIFY_CLIENT_ID "xiaozhi-passport-01"
#define CODEX_NOTIFY_TOPIC_PREFIX "devices/passport-01/notify/"
#define CODEX_NOTIFY_SUB_TOPIC "devices/passport-01/notify/+"
#define CODEX_NOTIFY_ACK_TOPIC "devices/passport-01/ack"
