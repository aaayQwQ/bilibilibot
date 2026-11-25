#include "anthropic.hpp"
#include "bilibili.hpp"
#include "config.hpp"
#include "plugin_api.hpp"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QTextStream>
#include <QTimer>
#include <QElapsedTimer>

struct ChatRequest {
    qint64 uid = 0;
    QString name;
    QString text;
};
struct PendingReply {
    ChatRequest request;
    QString answer;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("bilibilibot");
    QCoreApplication::setApplicationVersion("1.0.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("Bilibili live-room AI danmaku bot");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOption(QStringList{"c", "config"}, "INI configuration file", "path", "config.ini");
    parser.addOption(configOption);
    parser.process(app);

    Config config;
    QString configError;
    if (!config.load(parser.value(configOption), &configError)) {
        QTextStream(stderr) << "[ERROR] " << configError << Qt::endl;
        return 2;
    }

    QTextStream out(stdout);
    auto log = [&out](const QString &message) {
        out << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
            << "] " << message << Qt::endl;
    };
    log("bilibilibot 1.0.1 启动。");
    log(QString("直播间：%1；关键词：%2").arg(config.roomId).arg(config.keywords.join(" | ")));
    log("运行数据仅保存在内存中；按 Ctrl+C 退出。");

    BilibiliClient bilibili(config.roomId, config.cookie, config.csrf);
    AIClient ai(config);
    const QString pluginDirectory = QFileInfo(parser.value(configOption)).absoluteDir().filePath("plugins");
    PluginManager plugins(pluginDirectory, config.pluginsEnabled, config.pluginSendDanmakuEnabled,
                          [&bilibili](const QString &message) { bilibili.sendDanmaku(message); }, &app);
    QObject::connect(&plugins, &PluginManager::log, &app, log);
    plugins.load();
    QObject::connect(&bilibili, &BilibiliClient::eventReceived, &plugins, &PluginManager::dispatch);

    QHash<qint64, QJsonArray> histories;
    QHash<qint64, qint64> userLastReplyMs;
    QHash<QString, qint64> recentBotMessagesMs;
    QHash<quint64, PendingReply> pendingReplies;
    QQueue<ChatRequest> queue;
    QElapsedTimer monotonicClock;
    monotonicClock.start();
    quint64 nextSendId = 1;
    bool busy = false;

    auto processNext = [&]() {
        if (busy || queue.isEmpty()) return;
        const ChatRequest request = queue.dequeue();
        busy = true;
        log(QString("AI 正在处理：%1（UID %2）").arg(request.name).arg(request.uid));
        ai.generate(request.uid, request.name, request.text, histories.value(request.uid));
    };

    QObject::connect(&bilibili, &BilibiliClient::log, &app, log);
    QObject::connect(&bilibili, &BilibiliClient::identityReady, &app, [&log](qint64 uid) {
        log(QString("已识别机器人账号 UID：%1").arg(uid));
    });
    QObject::connect(&bilibili, &BilibiliClient::connected, &app, [&log]() {
        log("弹幕监听已就绪。");
    });

    QObject::connect(&bilibili, &BilibiliClient::danmaku, &app,
        [&](qint64 uid, const QString &name, const QString &text) {
            const qint64 now = monotonicClock.elapsed();
            for (auto it = recentBotMessagesMs.begin(); it != recentBotMessagesMs.end();) {
                if (now - it.value() > 45000) it = recentBotMessagesMs.erase(it);
                else ++it;
            }
            if (uid == bilibili.ownUid() && recentBotMessagesMs.contains(text)) {
                recentBotMessagesMs.remove(text);
                log("忽略本程序已成功发送的弹幕回显，防止自触发循环。");
                return;
            }
            if (uid == bilibili.ownUid() && !config.replyToSelf) return;

            bool matched = false;
            for (const QString &keyword : config.keywords) {
                if (text.contains(keyword, Qt::CaseInsensitive)) { matched = true; break; }
            }
            if (!matched) return;

            const qint64 previous = userLastReplyMs.value(uid, -1);
            if (config.userCooldownSeconds > 0 && previous >= 0 &&
                now - previous < qint64(config.userCooldownSeconds) * 1000) {
                log(QString("忽略 %1：用户冷却中。").arg(name));
                return;
            }
            static qint64 roomLastReplyMs = -1;
            if (config.roomCooldownSeconds > 0 && roomLastReplyMs >= 0 &&
                now - roomLastReplyMs < qint64(config.roomCooldownSeconds) * 1000) {
                log(QString("忽略 %1：直播间回复冷却中。").arg(name));
                return;
            }
            if (queue.size() >= 50) {
                log("待处理队列已满，丢弃本次触发。");
                return;
            }
            for (auto it = userLastReplyMs.begin(); it != userLastReplyMs.end();) {
                if (now - it.value() > 3600000) it = userLastReplyMs.erase(it);
                else ++it;
            }
            userLastReplyMs.insert(uid, now);
            roomLastReplyMs = now;
            queue.enqueue(ChatRequest{uid, name.left(64), text.left(2000)});
            processNext();
        });

    QObject::connect(&ai, &AIClient::finished, &app,
        [&](qint64 uid, const QString &name, const QString &input, const QString &answer) {
            QString reply;
            reply.reserve(qMin(answer.size(), config.maxReplyChars));
            for (const QChar ch : answer) {
                const auto category = ch.category();
                if (category == QChar::Other_Control || category == QChar::Other_Format ||
                    category == QChar::Other_Surrogate || category == QChar::Other_NotAssigned)
                    continue;
                reply.append(ch);
            }
            reply = reply.simplified();
            if (reply.size() > config.maxReplyChars)
                reply = reply.left(config.maxReplyChars).trimmed();
            if (!reply.isEmpty()) {
                const quint64 requestId = nextSendId++;
                pendingReplies.insert(requestId,
                    PendingReply{ChatRequest{uid, name, input}, reply});
                log(QString("准备发送回复：%1").arg(reply));
                bilibili.sendDanmaku(reply, requestId);
            }
            busy = false;
            QTimer::singleShot(0, &app, processNext);
        });

    QObject::connect(&ai, &AIClient::failed, &app,
        [&](qint64, const QString &name, const QString &, const QString &reason) {
            log(QString("AI 请求失败（%1）：%2").arg(name, reason));
            busy = false;
            QTimer::singleShot(0, &app, processNext);
        });

    QObject::connect(&bilibili, &BilibiliClient::danmakuSendFinished, &app,
        [&](quint64 requestId, const QString &text, bool success, const QString &reason) {
            if (requestId == 0) return; // Plugin-originated sends have no AI history.
            auto it = pendingReplies.find(requestId);
            if (it == pendingReplies.end()) return;
            if (success) {
                const qint64 uid = it->request.uid;
                QJsonArray history = histories.value(uid);
                bool validPairs = (history.size() % 2 == 0);
                for (int i = 0; validPairs && i < history.size(); i += 2) {
                    validPairs = history.at(i).toObject().value("role").toString() == "user" &&
                        history.at(i + 1).toObject().value("role").toString() == "assistant";
                }
                if (!validPairs) history = QJsonArray();
                history.append(QJsonObject{{"role", "user"},
                    {"content", QString("%1：%2").arg(it->request.name, it->request.text)}});
                history.append(QJsonObject{{"role", "assistant"}, {"content", it->answer}});
                while (history.size() > config.memoryTurns * 2 && history.size() >= 2) {
                    history.removeAt(0);
                    history.removeAt(0);
                }
                if (config.memoryTurns > 0) {
                    if (!histories.contains(uid) && histories.size() >= 500)
                        histories.erase(histories.begin());
                    histories.insert(uid, history);
                }
                recentBotMessagesMs.insert(text, monotonicClock.elapsed());
            } else {
                log("AI 回复未被 Bilibili 接受，不加入回显去重表或对话历史：" + reason);
            }
            pendingReplies.erase(it);
        });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &bilibili, &BilibiliClient::stop);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &ai, &AIClient::stop);
    bilibili.start();
    return app.exec();
}
