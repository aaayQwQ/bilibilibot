#pragma once
#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QString>
#include <functional>
class PluginManager final : public QObject {
    Q_OBJECT
public:
    using SendDanmaku = std::function<void(const QString &)>;
    explicit PluginManager(QString directory, bool enabled, bool allowSendDanmaku,
                           SendDanmaku sender, QObject *parent = nullptr);
    void load();
    void dispatch(const QJsonObject &event);
    int pluginCount() const noexcept { return plugins_.size(); }
signals:
    void log(const QString &message);
private:
    struct Rule {
        QString id, event, messageTemplate;
        int cooldownSeconds = 0, minGiftCount = 1;
        qint64 lastTriggered = 0;
    };
    QString directory_;
    bool enabled_ = false, allowSendDanmaku_ = false;
    SendDanmaku sender_;
    QHash<QString, Rule> plugins_;
    qint64 lastGlobalSend_ = 0;
};
