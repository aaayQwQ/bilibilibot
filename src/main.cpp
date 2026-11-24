#include "anthropic.h"
#include "bilibili.h"
#include "config.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QTextStream>
#include <QTimer>

struct ChatRequest {
    qint64 uid = 0;
    QString name;
    QString text;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("bilibilibot");
    QCoreApplication::setApplicationVersion("0.1.0");

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

    log("bilibilibot 0.1.0 启动。");
    log(QString("直播间：%1；关键词：%2").arg(config.roomId).arg(config.keywords.join(" | ")));
    log("运行数据仅保存在内存中；按 Ctrl+C 退出。");

    BilibiliClient bilibili(config.roomId, config.cookie, config.csrf);
    AIClient ai(config);
    QHash<qint64, QJsonArray> histories;
    QHash<qint64, qint64> userLastReply;
    QHash<QString, qint64> recentBotMessages;
    QQueue<ChatRequest> queue;
    bool busy = false;
    ChatRequest current;

    auto processNext = [&]() {
        if (busy || queue.isEmpty()) return;
        current = queue.dequeue();
        busy = true;
        const QJsonArray history = histories.value(current.uid);
        log(QString("AI 正在处理：%1（UID %2）").arg(current.name).arg(current.uid));
        ai.generate(current.name, current.text, history);
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
            const qint64 now = QDateTime::currentSecsSinceEpoch();

            // Ignore only messages recently emitted by this process. Other messages
            // written by the bot account itself remain eligible for keyword matching.
            for (auto it = recentBotMessages.begin(); it != recentBotMessages.end();) {
                if (it.value() < now - 45) it = recentBotMessages.erase(it);
                else ++it;
            }
            if (uid == bilibili.ownUid() && recentBotMessages.contains(text)) {
                recentBotMessages.remove(text);
                log("忽略本程序刚刚发送的弹幕回显，防止自触发循环。");
                return;
            }
            if (uid == bilibili.ownUid() && !config.replyToSelf) return;

            bool matched = false;
            for (const QString &keyword : config.keywords) {
                if (text.contains(keyword, Qt::CaseInsensitive)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) return;

            const qint64 previous = userLastReply.value(uid, 0);
            if (config.userCooldownSeconds > 0 && now - previous < config.userCooldownSeconds) {
                log(QString("忽略 %1：用户冷却中。").arg(name));
                return;
            }
            static qint64 roomLastReply = 0;
            if (config.roomCooldownSeconds > 0 && now - roomLastReply < config.roomCooldownSeconds) {
                log(QString("忽略 %1：直播间回复冷却中。").arg(name));
                return;
            }
            // Bound all runtime-only state so a busy room cannot grow RAM forever.
            if (queue.size() >= 50) {
                log("待处理队列已满，丢弃本次触发。");
                return;
            }
            for (auto it = userLastReply.begin(); it != userLastReply.end();) {
                if (it.value() < now - 3600) it = userLastReply.erase(it);
                else ++it;
            }
            userLastReply.insert(uid, now);
            roomLastReply = now;
            queue.enqueue(ChatRequest{uid, name, text});
            processNext();
        });

    QObject::connect(&ai, &AnthropicClient::finished, &app, [&](const QString &answer) {
        QString reply = answer;
        reply.replace('\n', ' ');
        reply.replace('\r', ' ');
        reply = reply.simplified();
        if (reply.size() > config.maxReplyChars)
            reply = reply.left(config.maxReplyChars).trimmed();
        if (!reply.isEmpty()) {
            QJsonArray history = histories.value(current.uid);
            history.append(QJsonObject{{"role", "user"},
                {"content", QString("%1：%2").arg(current.name, current.text)}});
            history.append(QJsonObject{{"role", "assistant"}, {"content", reply}});
            const int maxMessages = config.memoryTurns * 2;
            while (history.size() > maxMessages && !history.isEmpty())
                history.removeAt(0);
            if (config.memoryTurns > 0) {
                if (!histories.contains(current.uid) && histories.size() >= 500)
                    histories.erase(histories.begin());
                histories.insert(current.uid, history);
            }
            recentBotMessages.insert(reply, QDateTime::currentSecsSinceEpoch());
            log(QString("准备发送回复：%1").arg(reply));
            bilibili.sendDanmaku(reply);
        }
        busy = false;
        QTimer::singleShot(0, &app, processNext);
    });
    QObject::connect(&ai, &AnthropicClient::failed, &app, [&](const QString &reason) {
        log("AI 请求失败：" + reason);
        busy = false;
        QTimer::singleShot(0, &app, processNext);
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &bilibili, &BilibiliClient::stop);
    bilibili.start();
    return app.exec();
}
