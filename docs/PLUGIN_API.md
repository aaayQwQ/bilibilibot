# Plugin API v1

bilibilibot 1.0.0 supports declarative JSON plugins only. A manifest describes a room event filter and one approved action. It is data, not executable code.

## Supported events

All event objects contain a type and Unix timestamp.

| type | fields |
|---|---|
| danmaku | uid, user, text |
| gift | uid, user, gift_id, gift, count, price, total_coin, coin_type |
| superchat | uid, user, message, price, message_id |
| guard_buy | uid, user, guard_level, count, price |

Amounts are passed through as reported by the Bilibili event payload; the plugin API does not convert currencies or infer display units.

## Manifest

Place a JSON file in the plugins directory beside the active config.ini:

    {
      "schema_version": 1,
      "id": "gift-thanks",
      "enabled": true,
      "event": "gift",
      "min_gift_count": 1,
      "cooldown_seconds": 15,
      "action": {
        "type": "send_danmaku",
        "template": "感谢 {user} 赠送 {gift} x{count}！"
      }
    }

Allowed event values: danmaku, gift, superchat, guard_buy.

Allowed action: send_danmaku only.

Allowed template variables: {user}, {gift}, {count}, {message}, {price}, {guard_level}.

## Permission gates

Both settings must be enabled:

    [plugins]
    enabled=true
    allow_send_danmaku=true

The default for both is false. If the action permission is not granted, plugin events are ignored and no message is sent.

## Resource limits

- Maximum 32 manifests.
- Maximum 64 KiB per manifest.
- Maximum 180 characters per emitted message.
- Per-plugin cooldown: 0–3600 seconds.
- Gift minimum count: 1–100000.
- Unknown event types and action types are rejected.
- Symlink manifests are ignored.
- No plugin shell, process, filesystem-write, socket, or arbitrary network API exists.

## Compatibility

This API is not a binary compatibility layer for plugins written for other bots. Those plugins often depend on a specific host ABI or scripting engine. Adapters may be written for documented event formats, but arbitrary native plugins must not be loaded into the main process: C++ code running in-process has the same OS permissions as the bot.
