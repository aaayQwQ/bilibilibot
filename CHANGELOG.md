# Changelog

所有重要变更均记录于此。

## [1.0.0] - 2026-11-25

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
- Negotiate uncompressed WebSocket frames and reject unnegotiated compressed payloads to avoid unbounded decompression.
- Do not load arbitrary native plugins or execute scripts/processes.

### Build
- Standardize on Qt 6 and C++17.
- Add Windows, Linux and macOS CI builds and downloadable artifacts.
