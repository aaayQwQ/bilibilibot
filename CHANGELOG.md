# Changelog

## [1.0.3] - 2026-11-26

### 安全与稳定性
- 对 Bilibili 消息字段和插件事件的 JSON 数值进行有限性及 qint64 边界检查，拒绝非有限或越界 double；Bilibili 房间/账号 ID 同样使用安全转换。
- AI 响应体设置 2 MiB 上限，弹幕发送 API 响应体设置 64 KiB 上限；流式读取时超过上限立即中止请求。
- 配置文件与 cookie.txt 在读取前尝试收紧为所有者读写权限；权限设置失败输出警告。Windows 需检查实际 NTFS ACL，Qt 权限接口不构成 ACL 保证。
- 按 UTF-16 代理对处理文本，保留合法 emoji/非 BMP 字符并过滤孤立代理项。
- 默认不输出 AI 回复全文；仅在显式启用 logging.log_ai_content 后记录，redact_ai_content 默认开启并对常见凭据字段脱敏。
- Cookie 文件拒绝无效 UTF-8。

### 测试与供应链
- 新增 Bilibili 协议包头截断、长度越界、未知操作码、拼接包、包数上限和超大解压负载测试。
- 新增插件极端数值和补充平面 Unicode 测试；新增无效 UTF-8 与过期 Cookie 测试。
- 增加可选 ASan/UBSan 构建及 Linux CI Sanitizer job。
- GitHub Actions 外部依赖固定到经过核对的完整提交 SHA，并启用 Dependabot 每周检查 GitHub Actions 依赖更新。

### 已知边界
- 文件权限收紧失败时程序会警告但继续启动；Windows 上必须由管理员/用户核查 NTFS ACL。
- AI 内容脱敏仅覆盖常见凭据格式，不能保证移除所有个人信息；因此全文日志默认关闭。
- AI HTTPS 行为尚未加入本地 HTTPS mock 服务的完整端到端测试；真实服务商、真实 Cookie 和直播间的集成行为仍需部署环境验证。


所有重要变更均记录于此。

## [1.0.2] - 2026-11-25

### Build fixes
- Include QDir explicitly before using QFileInfo::absoluteDir(), fixing Windows, Linux and macOS compilation.
- Use consistently typed qint64 bounds for gift and guard counts, fixing ambiguous qBound overload resolution across MSVC, GCC and Clang.
- Require all three platform builds, Qt Test and CodeQL to pass before publishing v1.0.2.

## [1.0.1] - 2026-11-25

### 安全与稳定性
- 修复插件发送日志不可达问题；插件按 ID 排序执行，重复 ID 明确拒绝。
- 插件冷却使用单调时钟，避免系统时间回拨和秒级精度问题。
- 插件模板变量过滤控制字符、Unicode 格式控制字符，并限制变量与完整弹幕长度；保留合法的补充平面 Unicode 字符。
- 礼物、舰长和醒目留言字段支持数字字符串及字段别名。
- WBI 图片密钥增加 32 位字符格式校验。
- AI 完成/失败信号携带 UID、用户名和原始输入，避免异步结果归属错误。
- 对话历史校验 user/assistant 配对，并按完整轮次回收。
- 仅在 Bilibili API 明确接受弹幕后登记回显去重和对话历史。
- AI 与插件弹幕共用有界串行发送队列，限制发送频率。
- AI Base URL 增加精确主机白名单，拒绝 IP 字面量和常见本地域名。
- AI 系统提示明确观众消息为不可信输入；不向模型提供 Cookie 或 API Key。
- 尽力将配置文件和 cookie.txt 权限收紧为所有者读写；Windows 最终 ACL 仍由账户和目录策略决定。\n- 拒绝符号链接 Cookie 文件、符号链接插件目录，并将 Cookie 文件大小限制为 1 MiB。
- 空解压负载不再递归解析。

### CI 与发布
- Windows x64、Linux x64、macOS x64 构建与 Qt Test 全部通过且 CodeQL 成功后，才自动创建 v1.0.1 Release；v1.0.0 保持不变。
- CI 按分支合并并取消过期排队任务，避免旧提交构建挤占最新版本验证。

### 测试
- 扩展 Qt Test，覆盖插件冷却、数字字符串、未知占位符、控制字符、超长模板、异常 JSON、重复 ID、符号链接、插件数量上限，以及 Netscape Cookie、Cookie 符号链接、AI 主机白名单和非法端点配置。


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
