# Changelog

所有重要变更均记录于此。

## [1.0.0] - 2026-11-25

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
