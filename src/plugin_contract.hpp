#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace PluginContract {

constexpr int ApiVersion = 1;
constexpr qsizetype MaxEventBytes = 64 * 1024;
constexpr qsizetype MaxActionBytes = 8 * 1024;
constexpr int MaxActionsPerCall = 8;

struct Descriptor {
    QString id;
    QString version;
    int apiVersion = ApiVersion;
    QStringList subscriptions;
    QStringList requestedCapabilities;
};

struct ActionRequest {
    QString type;
    QString text;
};

bool validDescriptor(const Descriptor &descriptor, QString *error = nullptr);

bool validateEvent(const QByteArray &data,
                   QJsonObject *event = nullptr,
                   QString *error = nullptr);

bool validateActions(const QByteArray &data,
                     QList<ActionRequest> *actions = nullptr,
                     QString *error = nullptr);

} // namespace PluginContract
