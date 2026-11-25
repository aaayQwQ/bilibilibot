#pragma once
#include "config.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QSet>

class QNetworkReply;

class AIClient final : public QObject {
    Q_OBJECT
public:
    explicit AIClient(const Config &config, QObject *parent = nullptr);
    void generate(qint64 uid, const QString &userName, const QString &message, const QJsonArray &history);
    void stop();

signals:
    void finished(qint64 uid, const QString &userName, const QString &input, const QString &reply);
    void failed(qint64 uid, const QString &userName, const QString &input, const QString &reason);

private:
    Config config_;
    QNetworkAccessManager network_;
    QSet<QNetworkReply *> pendingReplies_;
    bool stopped_ = false;
};
