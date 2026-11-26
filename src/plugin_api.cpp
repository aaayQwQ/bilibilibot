#include "plugin_api.hpp"
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <cmath>
#include <QStringList>
#include <utility>

namespace {
constexpr qint64 MaxManifestBytes = 64 * 1024;
constexpr int MaxPlugins = 32;
constexpr int MaxMessageLength = 180;
constexpr int MaxVariableLength = 80;

bool validId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,47}$"));
    return re.match(id).hasMatch();
}

QString safeField(QString value) {
    QString clean;
    clean.reserve(qMin<qsizetype>(value.size(), MaxVariableLength));
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch.isHighSurrogate() && i + 1 < value.size() && value.at(i + 1).isLowSurrogate()) {
            if (clean.size() + 2 > MaxVariableLength) break;
            clean.append(ch);
            clean.append(value.at(++i));
            continue;
        }
        const auto category = ch.category();
        if (category == QChar::Other_Control || (category == QChar::Other_Format && ch.unicode() != 0x200D) ||
            category == QChar::Other_Surrogate || category == QChar::Other_NotAssigned)
            continue;
        if (clean.size() >= MaxVariableLength) break;
        clean.append(ch);
    }
    return clean.simplified();
}

qint64 integerField(const QJsonObject &object, const QStringList &keys, qint64 fallback = 0) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble()) {
            const double number = value.toDouble();
            constexpr double MinInclusive = -9223372036854775808.0;
            constexpr double MaxExclusive = 9223372036854775808.0;
            if (std::isfinite(number) && number >= MinInclusive && number < MaxExclusive)
                return static_cast<qint64>(number);
        }
        if (value.isString()) {
            bool ok = false;
            const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
            if (ok) return parsed;
        }
    }
    return fallback;
}

QString stringField(const QJsonObject &object, const QStringList &keys) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().isEmpty()) return safeField(value.toString());
    }
    return {};
}
}

PluginManager::PluginManager(QString directory, bool enabled, bool allowSendDanmaku,
                             SendDanmaku sender, QStringList allowedPluginIds, QObject *parent)
    : QObject(parent), directory_(std::move(directory)), enabled_(enabled),
      allowSendDanmaku_(allowSendDanmaku),
      broker_(ActionBroker::Policy{enabled, allowSendDanmaku,
          QSet<QString>(allowedPluginIds.cbegin(), allowedPluginIds.cend()), 180, 3000},
          std::move(sender)) {
    monotonicClock_.start();
}

void PluginManager::load() {
    if (stopping_) return;
    plugins_.clear();
    if (!enabled_) { emit log(QStringLiteral("插件系统处于关闭状态。")); return; }
    if (!allowSendDanmaku_)
        emit log(QStringLiteral("插件发送弹幕权限未授权，写操作保持关闭。"));
    const QFileInfo directoryInfo(directory_);
    if (directoryInfo.isSymLink()) {
        emit log(QStringLiteral("插件目录是符号链接，拒绝加载。"));
        return;
    }
    QDir dir(directory_);
    if (!dir.exists()) { emit log(QStringLiteral("插件目录不存在：%1").arg(dir.absolutePath())); return; }
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
    int scanned = 0;
    for (const QFileInfo &info : files) {
        if (scanned++ >= MaxPlugins) {
            emit log(QStringLiteral("插件清单超过 %1 个上限，剩余文件已忽略。").arg(MaxPlugins));
            break;
        }
        if (info.isSymLink() || info.size() <= 0 || info.size() > MaxManifestBytes) continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.read(MaxManifestBytes + 1);
        file.close();
        if (bytes.size() > MaxManifestBytes) continue;
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            emit log(QStringLiteral("跳过 JSON 格式错误的插件清单：%1").arg(info.fileName()));
            continue;
        }
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
            emit log(QStringLiteral("跳过无效或重复 ID 的插件清单：%1").arg(info.fileName()));
            continue;
        }
        Rule rule;
        rule.id = id;
        rule.event = event;
        rule.messageTemplate = message;
        rule.cooldownSeconds = qBound(0, root.value("cooldown_seconds").toInt(10), 3600);
        rule.minGiftCount = qBound(1, root.value("min_gift_count").toInt(1), 100000);
        plugins_.insert(id, rule);
    }
    emit log(QStringLiteral("声明式插件已加载：%1 个。").arg(plugins_.size()));
}

void PluginManager::stop() {
    stopping_ = true;
    pendingRules_.clear();
    plugins_.clear();
}

void PluginManager::dispatch(const QJsonObject &event) {
    if (stopping_ || !enabled_) return;
    const QString eventType = event.value("type").toString();
    const qint64 now = monotonicClock_.elapsed();
    if (pendingGlobalSend_ || (lastGlobalSendMs_ >= 0 && now - lastGlobalSendMs_ < 3000)) return;

    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        Rule &rule = it.value();
        if (pendingRules_.contains(rule.id) || rule.event != eventType ||
            (rule.lastTriggeredMs >= 0 && now - rule.lastTriggeredMs < qint64(rule.cooldownSeconds) * 1000))
            continue;
        const qint64 count = integerField(event, {"count", "num"}, 0);
        if (eventType == "gift" && count < rule.minGiftCount) continue;

        QString text = rule.messageTemplate;
        text.replace("{user}", stringField(event, {"user", "username", "uname"}));
        text.replace("{gift}", stringField(event, {"gift", "giftName", "gift_name"}));
        text.replace("{count}", QString::number(count));
        text.replace("{message}", stringField(event, {"message", "msg"}));
        text.replace("{price}", QString::number(integerField(event, {"price", "total_coin"})));
        text.replace("{guard_level}", QString::number(integerField(event, {"guard_level", "guardLevel"})));
        static const QRegularExpression unresolved(QStringLiteral("\\{[^}]{1,40}\\}"));
        if (unresolved.match(text).hasMatch()) continue;
        text = safeField(text);
        if (text.isEmpty() || text.size() > MaxMessageLength) continue;

        const QString ruleId = rule.id;
        pendingRules_.insert(ruleId);
        QPointer<PluginManager> guard(this);
        const auto result = broker_.submit(ruleId, QStringLiteral("send_danmaku"), text,
            [guard, ruleId](bool success) {
            if (!guard || guard->stopping_) return;
            guard->pendingRules_.remove(ruleId);
            if (!success) {
                emit guard->log(QStringLiteral("插件 %1 的弹幕发送失败；冷却时间不启动。").arg(ruleId));
                return;
            }
            auto completedRule = guard->plugins_.find(ruleId);
            if (completedRule != guard->plugins_.end())
                completedRule->lastTriggeredMs = guard->monotonicClock_.elapsed();
            emit guard->log(QStringLiteral("插件 %1 的弹幕已被 Bilibili 接受，冷却时间开始。").arg(ruleId));
        });
        if (!result.accepted) {
            pendingRules_.remove(ruleId);
            emit log(QStringLiteral("插件 %1 的动作被宿主拒绝：%2。")
                .arg(ruleId, ActionBroker::errorName(result.error)));
            continue;
        }
        emit log(QStringLiteral("插件 %1 已提交弹幕发送请求。").arg(ruleId));
        break;
    }
}
