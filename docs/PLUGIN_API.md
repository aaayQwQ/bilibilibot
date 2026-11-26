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

Three gates must be satisfied to send danmaku:

    [plugins]
    enabled=true
    allow_send_danmaku=true
    allow_send_danmaku_plugins=gift-thanks

- `enabled` enables JSON plugin loading; default: `false`.
- `allow_send_danmaku` enables the global send-danmaku capability; default: `false`.
- `allow_send_danmaku_plugins` is a semicolon-separated allowlist of plugin IDs. It is empty by default. A plugin absent from this list cannot send, even when the two global switches are enabled.
- A manifest cannot grant a capability to itself. Removing an ID revokes that plugin's send permission; restart the application to apply the configuration.

Missing or malformed authorization fails closed. A plugin may still be loaded for read-only event processing when sending is not authorized.

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


## 宿主动作代理与逐插件授权

```ini
[plugins]
enabled=true
allow_send_danmaku=true
allow_send_danmaku_plugins=gift-thanks
```

旧的两个布尔项仍是全局开关。除此之外，插件 ID 必须明确出现在 `allow_send_danmaku_plugins` 中才有发送弹幕能力；该列表缺失或为空时，生产装配拒绝所有插件写操作。撤销 ID 后重启即可撤销授权。插件清单中的自我声明不会授予权限。

所有 JSON 插件写动作统一通过宿主 ActionBroker。它校验动作类型、文本、180 字符上限、单个在途动作和成功后的全局发送间隔。BilibiliClient 仍是平台传输层，不向插件暴露凭据。

## 版本化序列化契约

内部序列化契约使用 `api_version: 1`。规范化事件最大 64 KiB；动作结果最大 8 KiB、每次最多 8 个动作。未知 API 版本、事件、动作、无效 JSON 和超限输入均拒绝。此校验器是宿主侧 JSON 契约，不代表 Wasm 引擎已经可用。

## Wasm 状态

本分支尚未实现 Wasm 加载。没有运行时、WASI 能力配置、fuel 计量、线性内存限制或 worker 进程监督器。不得将 JSON API 描述为二进制或可执行插件 ABI。
