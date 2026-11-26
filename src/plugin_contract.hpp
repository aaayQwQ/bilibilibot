#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QList>
namespace PluginContract {
constexpr int ApiVersion=1;constexpr qsizetype MaxEventBytes=65536;constexpr qsizetype MaxActionBytes=8192;constexpr int MaxActionsPerCall=8;
struct Descriptor{QString id,version;int apiVersion=ApiVersion;QStringList subscriptions,requestedCapabilities;};
struct ActionRequest{QString type,text;};
bool validDescriptor(const Descriptor&,QString*error=nullptr);
bool validateEvent(const QByteArray&,QJsonObject*event=nullptr,QString*error=nullptr);
bool validateActions(const QByteArray&,QList<ActionRequest>*actions=nullptr,QString*error=nullptr);
}