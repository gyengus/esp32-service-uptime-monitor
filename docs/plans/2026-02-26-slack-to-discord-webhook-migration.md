# Slack → Discord Webhook Migration Plan

**Date:** 2026-02-26
**Status:** Completed

## Context

The project currently uses Slack webhooks for sending notifications. The user wants to switch to Discord webhooks, which requires a different JSON format.

**Current Slack format:**
```json
{"text": "message"}
```

**Target Discord format (Embed):**
```json
{
  "embeds": [{
    "title": "Status Change Alert",
    "description": "message",
    "color": 3447003
  }]
}
```

## Decisions

| Question | Decision |
|----------|----------|
| Naming | Discord-specific (`discordWebhookURL`) |
| Message format | Discord Embed (rich formatting) |
| Backward compatibility | Automatic migration from old config |

## Affected Files

### Backend (C++)

| File | Change |
|------|--------|
| `include/Config.h` | `slackWebhookURL` → `discordWebhookURL` |
| `src/Config.cpp` | JSON key change + migration logic |
| `src/NotificationService.cpp` | `{"text": ...}` → `{"embeds": [...]}` |
| `src/MonitoringService.cpp` | Markdown formatting (`*` → `**`) |
| `src/Api.cpp` | `/api/testSlack` → `/api/testDiscord` |

### Frontend

| File | Change |
|------|--------|
| `web/config.html` | Label, input ID, icon (`mdi-slack` → `mdi-discord`) |
| `web/static/app.config.js` | Field names, endpoint, messages |

### Documentation

| File | Change |
|------|--------|
| `config.example.json` | `discordWebhookURL` example |
| `README.md` | Slack → Discord update |
| `CLAUDE.md` | Slack references update |

## Implementation Steps

### 1. Config.h - Struct Field Rename

```cpp
// Old
String slackWebhookURL;

// New
String discordWebhookURL;
```

### 2. Config.cpp - Serialization + Migration

**fillJson():**
```cpp
doc["discordWebhookURL"] = config.discordWebhookURL;
```

**fromJsonObject() - Migration logic:**
```cpp
// Backward compatibility: migrate old field
if (doc.containsKey("slackWebhookURL") && !doc.containsKey("discordWebhookURL")) {
    config.discordWebhookURL = doc["slackWebhookURL"] | "";
} else {
    config.discordWebhookURL = doc["discordWebhookURL"] | "";
}
```

### 3. NotificationService.cpp - Payload Format

**Old (Slack):**
```cpp
doc["text"] = message;
```

**New (Discord Embed):**
```cpp
JsonDocument doc;
JsonArray embeds = doc["embeds"].to<JsonArray>();
JsonObject embed = embeds.add<JsonObject>();
embed["title"] = "ESP32 Service Uptime Monitor";
embed["description"] = message;
embed["color"] = 3447003;  // Discord blue
```

### 4. MonitoringService.cpp - Markdown Formatting

Discord uses `**bold**` syntax instead of Slack's `*bold*`:

```cpp
// Old
msg = "⚠️ *Status Change Alert*\n";
msg += "Entity: *" + entity->getName() + "*\n";

// New
msg = "Status Change Alert\n";
msg += "Entity: **" + entity->getName() + "**\n";
```

### 5. Api.cpp - Endpoint Rename

```cpp
// Old
server.on("/api/testSlack", HTTP_POST, ...);

// New
server.on("/api/testDiscord", HTTP_POST, ...);
```

Test message update:
```cpp
notificationService.send(testUrl, "Your Discord integration is working perfectly!");
```

### 6. config.html - UI Elements

```html
<!-- Old -->
<label for="slackWebhook">Slack webhook</label>
<input id="slackWebhookURL" />
<span class="mdi mdi-slack"></span>

<!-- New -->
<label for="discordWebhook">Discord webhook</label>
<input id="discordWebhookURL" />
<span class="mdi mdi-discord"></span>
```

### 7. app.config.js - JavaScript

```javascript
// Config loading
$('#discordWebhookURL').val(config.discordWebhookURL);

// Config saving
discordWebhookURL: $('#discordWebhookURL').val()

// Test endpoint
$.post('/api/testDiscord', { url: webhook })

// Feedback
showFeedback("Test message sent! Check your Discord channel.");
```

## Verification

1. [x] Fresh install with new config
2. [x] Upgrade from old config with `slackWebhookURL` (migration test)
3. [x] Notification sending on entity status change
4. [x] Test button functionality in UI
5. [x] Embed formatting verification in Discord
6. [x] API endpoint `/api/testDiscord` availability

## Implementation Order

1. Backend Config (Config.h, Config.cpp)
2. NotificationService
3. MonitoringService
4. API Endpoints (Api.cpp)
5. Frontend HTML (config.html)
6. Frontend JS (app.config.js)
7. Documentation (README.md, CLAUDE.md, config.example.json)