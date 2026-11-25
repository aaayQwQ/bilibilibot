# bilibilibot 1.0.0 — 弹幕有回应，直播更有趣

**Qt 6 · C++17 · 跨平台 · 安全优先**

轻量级 Bilibili 直播间机器人，通过 Qt Network / WebSockets 连接弹幕服务，可接入 OpenAI 兼容或 Anthropic Messages API，并提供受限的 JSON 声明式插件接口。

## 1.0.0 新功能

- 新增礼物、醒目留言、舰长购买和普通弹幕事件 API。
- 新增 JSON 声明式插件；插件不执行脚本或本机代码。
- 插件总开关与发送弹幕权限分离，默认全部关闭。
- AI 服务强制 HTTPS；敏感请求不自动跟随重定向。
- 为网络请求设置超时、读取缓冲和 WebSocket 消息上限。
- 移除直接调用 zlib C API，改用 Qt 压缩接口并限制解压数据大小。
- 加入 Qt Test 自动化测试和 GitHub CodeQL C++ 安全扫描。
- GitHub Actions 构建 Windows x64、Linux x64、macOS x64。

## 技术栈

- Qt 6.5+：Core、Network、WebSockets
- C++17，CMake 3.21+
- 项目仅包含 C++ 源代码，不包含 C 源文件或直接调用 C 语言 API。
- 无额外第三方运行时依赖。

## 安全模型

默认禁止 Shell 命令执行、子进程、任意文件写入/删除、本地 HTTP/IPC 服务、端口监听、动态加载 DLL/SO/DYLIB、插件自建网络连接，以及插件读取 Cookie/API Key。

插件仅从配置文件同目录的 plugins 子目录读取 JSON 清单。单个清单最大 64 KiB，最多 32 个，符号链接忽略。支持事件：danmaku、gift、superchat、guard_buy。当前唯一动作是 send_danmaku。

启用插件需要在 config.ini 中分别显式设置：

    [plugins]
    enabled=true
    allow_send_danmaku=true

两项默认均为 false。模板变量：{user}、{gift}、{count}、{message}、{price}、{guard_level}。

**安全边界说明：** 原生 C++ 动态插件与宿主进程共享权限，无法仅靠 API 限制其任意代码行为。因此 1.0.0 不加载第三方原生插件。后续若要执行任意代码插件，必须先实现独立低权限进程和操作系统级沙箱。

## 配置和运行

1. 复制 config.ini.example 为 config.ini。
2. 填写直播间号及 AI 服务配置。
3. 将 Netscape 格式的 Bilibili Cookie 文件放在配置文件同目录，命名为 cookie.txt。
4. 运行 bilibilibot。

请勿提交 Cookie 或 API Key。建议使用专门的低权限系统账户运行。

## 构建

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel
    ctest --test-dir build --output-on-failure

Windows MSVC 需要安装 Qt 6 对应的 MSVC Kit。GitHub Actions 会自动构建三平台并上传构建产物；Windows 包通过 windeployqt 收集 Qt DLL 和运行库。

## 插件示例

仓库中的 plugins/gift-thanks.json 是礼物感谢示例。复制到程序配置目录下的 plugins 子目录，并显式开启上述两项权限即可启用。

## 兼容性

不同 Bilibili 机器人插件的宿主 ABI、脚本引擎、回调协议和权限模型并不统一。1.0.0 提供统一事件字段和安全动作接口，不能直接加载其他机器人专用 DLL、SO 或脚本。后续可按公开协议逐个编写适配器。

## 更新日志

### v1.0.0 —「弹幕生态，正式开场」 (2026-11-25)

- 礼物、醒目留言、舰长购买事件正式接入。
- JSON 声明式插件 API 与礼物感谢示例上线。
- 插件默认关闭，发送弹幕需要二次授权。
- AI 请求强制 HTTPS，关闭敏感请求的自动重定向。
- 增加网络超时、缓冲区及压缩数据大小限制。
- 移除项目对 zlib C API 的直接调用。
- 增加 Qt Test 与 GitHub CodeQL C++ 安全分析。
- 统一版本号为 1.0.0。

---

*让每一条弹幕，都有机会成为直播间的高光时刻。*
