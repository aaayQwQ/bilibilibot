# bilibilibot

C++17 Bilibili live-room chatbot. Runtime chat state and short conversation context are memory-only. The application reads `config.ini`; it does not write chat logs, cookies, prompts, or API responses to disk.

## Features

- Bilibili live danmaku WebSocket connection using cookie-authenticated room APIs.
- Automatic heartbeat and reconnect.
- Keyword-triggered AI replies through the Anthropic Messages API protocol (including Anthropic-compatible gateways such as SystemOne).
- Configurable model, API key, Base URL and adaptive thinking switch.
- Per-user and room cooldowns, bounded in-memory conversation context, duplicate suppression.
- No shell execution, local HTTP server, IPC, plugin loading, or host automation.

**Isolation note:** this is an ordinary user-space process, not an OS sandbox. It only reads its INI file and makes the configured HTTPS/WSS requests, but the operating system still grants it the permissions of the account that launches it. Use a dedicated low-privilege account or an OS container/sandbox if a hard security boundary is required.

## Dependencies

- CMake 3.21+
- Qt 6.2+ modules: Core, Network, WebSockets
- zlib
- C++17 compiler

## Build

### Windows (PowerShell)

Install a Qt 6 kit and CMake, then from the repository root:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release
```

### Linux / macOS

Install Qt 6 development packages, CMake and zlib development headers:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

Copy `config.ini.example` to `config.ini`, edit it, then run the executable from the repository root:

```sh
./build/bilibilibot
```

On Windows the executable is usually under `build/Release/bilibilibot.exe`.

## Configuration

- `bilibili.room_id`: live room ID.
- `bilibili.cookie`: browser Cookie header value. Keep it private; do not commit it.
- `ai.base_url`: Anthropic-compatible API base URL. The program appends `/v1/messages`.
- `ai.api_key`, `ai.model`: API credentials and model.
- `ai.thinking`: sends `thinking: { "type": "adaptive" }` when true. The selected model and gateway must support adaptive thinking.
- `bot.keywords`: `|` separated trigger strings.

The bot obtains the authenticated account UID from Bilibili's navigation API. When `reply_to_self=true`, that account's own incoming messages may trigger replies too. Messages just sent by this process are tracked in memory and ignored if echoed back, preventing self-reply loops.

## Privacy and operational limits

- No chat history is written to disk. Per-user context and sent-message fingerprints exist only in RAM and disappear at exit.
- The INI file is read at startup. Protect it because it contains session cookies and API credentials.
- Outbound connections are limited to Bilibili APIs/WebSocket and the configured AI endpoint.
- Do not share your Cookie or API key. Revoke the Bilibili session if the Cookie is exposed.
