#pragma once
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QSet>
#include <functional>
#include "action_broker.hpp"
class PluginManager final : public QObject {
    Q_OBJECT
public:
    using SendDanmaku = std::function<void(const QString &, std::function<void(bool)>)>;
    explicit PluginManager(QString directory, bool enabled, bool allowSendDanmaku,
                           SendDanmaku sender, QObject *parent = nullptr);
    PluginManager(QString directory, bool enabled, bool allowSendDanmaku,
                  SendDanmaku sender, QStringList allowedPluginIds, QObject *parent = nullptr);
    void stop();
    void load();
    void dispatch(const QJsonObject &event);
    int pluginCount() const noexcept { return plugins_.size(); }
signals:
    void log(const QString &message);
private:
    struct Rule {
        QString id, event, messageTemplate;
        int cooldownSeconds = 0, minGiftCount = 1;
        qint64 lastTriggeredMs = -1;
    };
    QString directory_;
    bool enabled_ = false, allowSendDanmaku_ = false;
    bool stopping_ = false;
    ActionBroker broker_;
    QMap<QString, Rule> plugins_;
    QElapsedTimer monotonicClock_;
    qint64 lastGlobalSendMs_ = -1;
    QSet<QString> pendingRules_;
    bool pendingGlobalSend_ = false;
};
