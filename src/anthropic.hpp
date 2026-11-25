#pragma once
#include "config.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonArray>

class AIClient final : public QObject {
    Q_OBJECT
public:
    explicit AIClient(const Config &config, QObject *parent = nullptr);
    void generate(const QString &userName, const QString &message, const QJsonArray &history);

signals:
    void finished(const QString &reply);
    void failed(const QString &reason);

private:
    Config config_;
    QNetworkAccessManager network_;
};
