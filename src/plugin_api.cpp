#include "plugin_api.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QDateTime>
#include <utility>
namespace {
constexpr qint64 MaxManifestBytes = 64 * 1024;
constexpr int MaxPlugins = 32;
constexpr int MaxMessageLength = 180;
bool validId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,47}$"));
    return re.match(id).hasMatch();
}
}
PluginManager::PluginManager(QString directory, bool enabled, bool allowSendDanmaku,
                             SendDanmaku sender, QObject *parent)
    : QObject(parent), directory_(std::move(directory)), enabled_(enabled),
      allowSendDanmaku_(allowSendDanmaku), sender_(std::move(sender)) {}
void PluginManager::load() {
    plugins_.clear();
    if (!enabled_) { emit log(QStringLiteral("插件系统处于关闭状态。")); return; }
    if (!allowSendDanmaku_ || !sender_)
        emit log(QStringLiteral("插件发送弹幕权限未授权，写操作保持关闭。"));
    QDir dir(directory_);
    if (!dir.exists()) { emit log(QStringLiteral("插件目录不存在：%1").arg(dir.absolutePath())); return; }
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
    int scanned = 0;
    for (const QFileInfo &info : files) {
        if (scanned++ >= MaxPlugins) break;
        if (info.isSymLink() || info.size() <= 0 || info.size() > MaxManifestBytes) continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.read(MaxManifestBytes + 1);
        file.close();
        if (bytes.size() > MaxManifestBytes) continue;
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const QJsonObject root = doc.object();
        const QJsonObject action = root.value("action").toObject();
        const QString id = root.value("id").toString();
        const QString event = root.value("event").toString();
        const QString type = action.value("type").toString();
        const QString message = action.value("template").toString();
        static const QStringList supported{"danmaku", "gift", "superchat", "guard_buy"};
        if (root.value("schema_version").toInt() != 1 ||
            !root.value("enabled").toBool(false) || !validId(id) ||
            plugins_.contains(id) || !supported.contains(event) ||
            type != "send_danmaku" || message.isEmpty() || message.size() > 200) {
            emit log(QStringLiteral("跳过无效插件清单：%1").arg(info.fileName()));
            continue;
        }
        Rule rule;
        rule.id = id; rule.event = event; rule.messageTemplate = message;
        rule.cooldownSeconds = qBound(0, root.value("cooldown_seconds").toInt(10), 3600);
        rule.minGiftCount = qBound(1, root.value("min_gift_count").toInt(1), 100000);
        plugins_.insert(id, rule);
    }
    emit log(QStringLiteral("声明式插件已加载：%1 个。").arg(plugins_.size()));
}
void PluginManager::dispatch(const QJsonObject &event) {
    if (!enabled_ || !allowSendDanmaku_ || !sender_) return;
    const QString eventType = event.value("type").toString();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        Rule &rule = it.value();
        if (rule.event != eventType || now - rule.lastTriggered < rule.cooldownSeconds) continue;
        if (eventType == "gift" && event.value("count").toInt() < rule.minGiftCount) continue;
        QString text = rule.messageTemplate;
        text.replace("{user}", event.value("user").toString());
        text.replace("{gift}", event.value("gift").toString());
        text.replace("{count}", QString::number(event.value("count").toInt()));
        text.replace("{message}", event.value("message").toString());
        text.replace("{price}", QString::number(event.value("price").toInt()));
        text.replace("{guard_level}", QString::number(event.value("guard_level").toInt()));
        static const QRegularExpression unresolved(QStringLiteral("\\{[^}]{1,40}\\}"));
        if (unresolved.match(text).hasMatch()) continue;
        text = text.simplified();
        if (text.isEmpty() || text.size() > MaxMessageLength) continue;
        rule.lastTriggered = now;
        sender_(text);
        emit log(QStringLiteral("插件 %1 已请求发送弹幕。").arg(rule.id));
    }
}
