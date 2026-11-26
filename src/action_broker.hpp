#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QSet>
#include <QString>

#include <functional>

class ActionBroker final : public QObject {
    Q_OBJECT

public:
    using Sender = std::function<void(const QString &, std::function<void(bool)>)>;

    enum class Error {
        None,
        Disabled,
        Unauthorized,
        InvalidAction,
        InvalidText,
        TooLong,
        Busy,
        RateLimited,
        SendFailed
    };

    struct Result {
        Error error = Error::None;
        bool accepted = false;
    };

    struct Policy {
        bool enabled = false;
        bool allowSendDanmaku = false;
        QSet<QString> allowedPluginIds;
        int maxMessageLength = 180;
        int minIntervalMs = 3000;
    };

    explicit ActionBroker(Policy policy, Sender sender, QObject *parent = nullptr);

    Result submit(const QString &pluginId,
                  const QString &action,
                  const QString &text,
                  std::function<void(bool)> completion = {});

    static QString errorName(Error error);

private:
    void finish(bool success, const std::function<void(bool)> &completion);

    Policy policy_;
    Sender sender_;
    QElapsedTimer clock_;
    qint64 lastSuccessMs_ = -1;
    bool inFlight_ = false;
};
