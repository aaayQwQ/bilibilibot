# 项目结构与阅读顺序

这份文档是源码导航，不是目标架构图。它描述的是 `modular-plugin-architecture` 分支当前真实存在的文件，避免把规划中的模块误认为已经实现。

## 先看这张地图

```text
bilibilibot/
├── README.md                 项目介绍、运行方式、安全边界
├── config.ini.example        配置项示例（不要放真实凭据）
├── CMakeLists.txt            可执行程序、Qt Test、构建选项
├── src/
│   ├── main.cpp              程序入口、对象装配、AI 回复流程
│   ├── config.hpp/.cpp       INI、Cookie、AI 主机白名单和配置校验
│   ├── bilibili.hpp/.cpp     Bilibili 登录、WebSocket、协议解析、弹幕发送
│   ├── anthropic.hpp/.cpp    AI API 请求与响应解析（兼容 OpenAI/Anthropic）
│   ├── plugin_api.hpp/.cpp   JSON manifest 加载、事件匹配、模板替换
│   ├── action_broker.hpp/.cpp 宿主动作准入、授权、清洗、限流和完成回调
│   └── plugin_contract.hpp/.cpp API v1 描述符、事件和动作 JSON 校验
├── plugins/
│   └── gift-thanks.json      礼物感谢示例 manifest
├── tests/
│   ├── test_config.cpp
│   ├── test_bilibili_protocol.cpp
│   ├── test_plugin_api.cpp
│   ├── test_action_broker.cpp
│   └── test_plugin_contract.cpp
├── docs/
│   ├── PROJECT_GUIDE.md      本文：源码地图和阅读路径
│   ├── PLUGIN_API.md         当前 JSON 插件 API
│   └── PLUGIN_ARCHITECTURE.md 目标设计、风险与分阶段迁移
└── .github/workflows/        Windows/Linux/macOS 构建、静态分析和发布流程
```

## 推荐阅读顺序

不要从头到尾硬读整个仓库。按你想理解的部分进入：

### A. 先弄懂程序怎么启动

1. `README.md`：知道程序用途、运行依赖和安全边界。
2. `config.ini.example`：看运行时有哪些配置。
3. `src/main.cpp`：看对象如何创建、信号如何连接、程序如何启动和退出。
4. `src/config.cpp`：看配置如何加载和校验。

### B. 看平台消息从哪里来、如何发出去

1. `src/bilibili.hpp`：先看对外信号和公共方法。
2. `src/bilibili.cpp`：再看登录、WebSocket 命令处理、事件发出和弹幕发送。
3. `tests/test_bilibili_protocol.cpp`：用测试理解协议输入和解析边界。

### C. 看 AI 自动回复

1. `src/anthropic.hpp`：看 AI 客户端接口。
2. `src/anthropic.cpp`：看请求构造、响应限制、错误和响应解析。
3. 回到 `src/main.cpp`：当前触发词、队列、冷却、历史和发送确认仍主要在入口中，尚未抽成独立应用服务。这是后续模块化的重点，不是已经完成的模块。

### D. 看插件与安全边界

建议按以下顺序读：

1. `plugins/gift-thanks.json`：先看一个实际插件清单。
2. `docs/PLUGIN_API.md`：理解事件、模板和授权配置。
3. `src/plugin_api.cpp`：看 manifest 校验、事件匹配与模板处理。
4. `src/action_broker.cpp`：看插件写动作如何被宿主授权和限流。
5. `src/plugin_contract.cpp`：看 API v1 序列化输入的大小、版本和字段检查。
6. `tests/test_plugin_api.cpp`、`tests/test_action_broker.cpp`、`tests/test_plugin_contract.cpp`：看对应测试。

**重要区别：** `plugin_contract` 当前是独立的契约校验组件；它不等于 Wasm ABI，也不表示 Wasm 插件加载器已经存在。当前实际加载的仍是 JSON 声明式规则。

## 关键数据流

### Bilibili 事件

```text
Bilibili WebSocket
    → BilibiliClient 解析协议
    → eventReceived(QJsonObject)
    → PluginManager::dispatch()
    → JSON 规则匹配与模板展开
    → ActionBroker::submit()
    → BilibiliClient::sendDanmaku()
    → 异步发送结果
    → PluginManager 更新冷却 / 输出日志
```

### AI 回复

```text
BilibiliClient 弹幕事件
    → main.cpp 过滤与排队
    → AIClient 请求服务商
    → main.cpp 处理 AI 回复
    → BilibiliClient 发送弹幕
    → 发送成功后更新对话历史与回显去重
```

AI 流程目前仍集中在 `main.cpp`，不要误以为它已经拆分成 ApplicationService。

## 构建与测试

在 Qt 6.5+、CMake 3.21+ 和 C++17 环境下：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure
```

启用 Linux/Clang/GCC 的 ASan 与 UBSan：

```sh
cmake -S . -B build-asan -DBILIBILIBOT_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## 当前目录为什么还没有继续拆成很多子目录？

现在的 `src/` 文件数量不多，但职责边界尚未完全稳定：例如 `main.cpp` 同时承载应用装配和 AI 业务策略，`bilibili.cpp` 同时承载连接、协议解析与发送。此时先把文件搬进很多目录，只会改变路径，不会真正降低耦合，还会增加 include 和 CMake 维护成本。

正确顺序是：先抽取可测试的接口和业务服务，再把稳定模块移动到 `app/`、`core/`、`platform/`、`services/`、`plugins/`。目录结构应反映已经存在的职责，而不是提前摆出尚未实现的架构。

## 实现状态的权威来源

- 当前 JSON 插件格式和权限：`docs/PLUGIN_API.md`
- 目标架构、风险和未实现事项：`docs/PLUGIN_ARCHITECTURE.md`
- 本文只负责告诉你“现在代码在哪里、应该先看什么”。
