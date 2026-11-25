# Changelog

所有重要变更均记录于此。

## [1.0.1] - 2026-11-25

### 安全与稳定性
- 修复插件发送日志不可达问题；插件按 ID 排序执行，重复 ID 明确拒绝。
- 插件冷却使用单调时钟，避免系统时间回拨和秒级精度问题。
- 插件模板变量过滤控制字符、Unicode 格式控制字符，并限制变量与完整弹幕长度。
- 礼物、舰长和醒目留言字段支持数字字符串及字段别名。
- WBI 图片密钥增加 32 位字符格式校验。
- AI 完成/失败信号携带 UID、用户名和原始输入，避免异步结果归属错误。
- 对话历史校验 user/assistant 配对，并按完整轮次回收。
- 仅在 Bilibili API 明确接受弹幕后登记回显去重和对话历史。
- AI 与插件弹幕共用有界串行发送队列，限制发送频率。
- AI Base URL 增加精确主机白名单，拒绝 IP 字面量和常见本地域名。
- AI 系统提示明确观众消息为不可信输入；不向模型提供 Cookie 或 API Key。
- 尽力将配置文件和 cookie.txt 权限收紧为所有者读写；Windows 最终 ACL 仍由账户和目录策略决定。
- 空解压负载不再递归解析。

### 测试
- 扩展插件 Qt Test，覆盖数字字符串、未知占位符、控制字符、超长模板、异常 JSON、重复 ID 和插件数量上限。


## [1.0.0] - 2026-11-25

### AI network request hotfix
- Avoid reading a closed `QNetworkReply` when a request is canceled, eliminating the `QIODevice::read: device not open` warning.
- Track outstanding AI requests and cancel them explicitly during application shutdown.
- Distinguish `OperationCanceledError` from HTTP/API errors and include request host, elapsed time, and reply-device state in diagnostics.
- Increase the transfer inactivity timeout to 120 seconds for reasoning models.

### Fixed
- Adapt WebSocket binary-message delivery through a lambda compatible with Qt's signal/slot arity rules.
- Negotiate Bilibili zlib payloads (`protover=2`) and enforce an 8 MiB decompressed-data ceiling using Qt APIs.
- Prevent reply callbacks from accessing a reply after scheduling deletion.
- Disconnect outstanding network callbacks before aborting requests during shutdown.

### Fixed
- Adapt WebSocket binary-message delivery through a lambda compatible with Qt's signal/slot arity rules.
- Negotiate Bilibili zlib payloads (`protover=2`) and enforce an 8 MiB decompressed-data ceiling using Qt APIs.
- Prevent reply callbacks from accessing a reply after scheduling deletion.
- Disconnect outstanding network callbacks before aborting requests during shutdown.

### Added
- Normalized room events for danmaku, gifts, Super Chat and guard purchases.
- JSON declarative plugin manifests with a restricted send-danmaku action.
- Explicit opt-in controls for plugin execution and outbound chat actions.
- Qt Test coverage for plugin permission checks and gift templates.
- GitHub CodeQL C++ security analysis.

### Security
- Require HTTPS for AI API endpoints.
- Disable automatic redirects for authenticated requests.
- Bound WebSocket frames/messages and network read buffers.
- Negotiate zlib WebSocket payloads and reject unsupported Brotli payloads; bound incoming and decompressed data.
- Do not load arbitrary native plugins or execute scripts/processes.

### Build
- Standardize on Qt 6 and C++17.
- Add Windows, Linux and macOS CI builds and downloadable artifacts.
