# bilibilibot — 弹幕有回应，直播更有趣

**Qt 6 · C++17 · 跨平台 · 安全优先**

轻量级 Bilibili 直播间机器人，通过 Qt Network / WebSockets 连接弹幕服务，可接入 OpenAI 兼容或 Anthropic Messages API，并提供受限的 JSON 声明式插件接口。

## 从哪里开始

- **第一次使用：** [配置与运行](#配置和运行)
- **想了解源码：** [项目结构与阅读顺序](docs/PROJECT_GUIDE.md)
- **想编写 JSON 插件：** [Plugin API v1](docs/PLUGIN_API.md)
- **想了解插件架构规划：** [插件架构与实际进度](docs/PLUGIN_ARCHITECTURE.md)
- **想查看变更：** [GitHub Releases](https://github.com/aaayQwQ/bilibilibot/releases)

## 版本与更新日志

项目版本、更新内容及发行说明统一发布在 GitHub Releases 中，README 不维护更新日志。

- [查看最新发行版](https://github.com/aaayQwQ/bilibilibot/releases/latest)
- [查看全部发行版与更新日志](https://github.com/aaayQwQ/bilibilibot/releases)

## 技术栈

- Qt 6.5+：Core、Network、WebSockets
- C++17，CMake 3.21+
- 项目仅包含 C++ 源代码，不包含 C 源文件或直接调用 C 语言 API。
- 无额外第三方运行时依赖。

## 安全模型

默认禁止 Shell 命令执行、子进程、任意文件写入/删除、本地 HTTP/IPC 服务、端口监听、动态加载 DLL/SO/DYLIB、插件自建网络连接，以及插件读取 Cookie/API Key。当前只加载受限的 JSON 声明式规则；JSON 是数据格式，不执行任意代码。

插件仅从配置文件同目录的 `plugins` 子目录读取 JSON 清单。单个清单最大 64 KiB，最多 32 个，符号链接忽略。支持事件：`danmaku`、`gift`、`superchat`、`guard_buy`。

启用插件需要在 config.ini 中分别显式设置：

    [plugins]
    enabled=true
    allow_send_danmaku=true
    allow_send_danmaku_plugins=gift-thanks

两项全局开关默认均为 false；插件 ID 白名单默认为空。只有列入白名单的插件才能发送弹幕。多个 ID 使用分号分隔。模板变量：`{user}`、`{gift}`、`{count}`、`{message}`、`{price}`、`{guard_level}`。

**安全边界说明：** 本版本没有 Lua/Wasm 引擎，也不加载第三方原生插件。未来计划优先评估受资源限制的 Wasm 运行时；在运行时隔离、燃料/内存限制、窄能力接口和三平台构建完成验证前，不应将脚本插件视为可用或安全。设计与迁移计划见 [插件架构方案](docs/PLUGIN_ARCHITECTURE.md)。

## 配置和运行

1. 复制 config.ini.example 为 config.ini。
2. 填写直播间号及 AI 服务配置；`[ai] allowed_hosts` 必须填写与 `base_url` 完全相同的主机名（例如 `api.example.com`），旧版 config.ini 需要补上这一项。
3. 将 Netscape 格式的 Bilibili Cookie 文件放在配置文件同目录，命名为 cookie.txt。
4. 运行 bilibilibot。

请勿提交 Cookie 或 API Key。建议使用专门的低权限系统账户运行。

## 构建

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel
    ctest --test-dir build --output-on-failure

Windows MSVC 需要安装 Qt 6 对应的 MSVC Kit。GitHub Actions 会自动构建三平台并上传构建产物；Windows 包通过 windeployqt 收集 Qt DLL 和运行库。

## 插件示例

仓库中的 `plugins/gift-thanks.json` 是礼物感谢示例。复制到程序配置目录下的 plugins 子目录，并显式开启插件开关、发送开关，同时将 `gift-thanks` 加入 `allow_send_danmaku_plugins` 白名单，才可启用感谢弹幕。当前插件接口及限制见 [Plugin API v1](docs/PLUGIN_API.md)。

## 兼容性

不同 Bilibili 机器人插件的宿主 ABI、脚本引擎、回调协议和权限模型并不统一。当前 API v1 是本项目自己的 JSON 声明式接口，不是其他机器人的二进制兼容层。

---

*让每一条弹幕，都有机会成为直播间的高光时刻。*
