#include "plugin_contract.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace PluginContract {
namespace {

bool fail(QString *error, const QString &reason) {
    if (error)
        *error = reason;
    return false;
}

bool isValidPluginId(const QString &id) {
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$"));
    return pattern.match(id).hasMatch();
}

const QStringList SupportedEvents{
    QStringLiteral("danmaku"),
    QStringLiteral("gift"),
    QStringLiteral("superchat"),
    QStringLiteral("guard_buy")
};

} // namespace

bool validDescriptor(const Descriptor &descriptor, QString *error) {
    if (!isValidPluginId(descriptor.id))
        return fail(error, QStringLiteral("invalid_plugin_id"));

    if (descriptor.version.trimmed().isEmpty() || descriptor.version.size() > 64)
        return fail(error, QStringLiteral("invalid_plugin_version"));

    if (descriptor.apiVersion != ApiVersion)
        return fail(error, QStringLiteral("unsupported_api_version"));

    if (descriptor.subscriptions.isEmpty() || descriptor.subscriptions.size() > 16)
        return fail(error, QStringLiteral("invalid_subscriptions"));

    for (const QString &event : descriptor.subscriptions) {
        if (!SupportedEvents.contains(event))
            return fail(error, QStringLiteral("unknown_event"));
    }

    for (const QString &capability : descriptor.requestedCapabilities) {
        if (capability != QStringLiteral("send_danmaku"))
            return fail(error, QStringLiteral("unknown_capability"));
    }

    return true;
}

bool validateEvent(const QByteArray &data, QJsonObject *event, QString *error) {
    if (data.isEmpty() || data.size() > MaxEventBytes)
        return fail(error, QStringLiteral("event_size_limit"));

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("invalid_event_json"));

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("api_version")).toInt(-1) != ApiVersion)
        return fail(error, QStringLiteral("unsupported_api_version"));

    if (!SupportedEvents.contains(object.value(QStringLiteral("type")).toString()))
        return fail(error, QStringLiteral("unknown_event"));

    if (event)
        *event = object;

    return true;
}

bool validateActions(const QByteArray &data,
                     QList<ActionRequest> *actions,
                     QString *error) {
    if (data.isEmpty() || data.size() > MaxActionBytes)
        return fail(error, QStringLiteral("action_size_limit"));

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("invalid_action_json"));

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("api_version")).toInt(-1) != ApiVersion)
        return fail(error, QStringLiteral("unsupported_api_version"));

    const QJsonValue actionValue = root.value(QStringLiteral("actions"));
    if (!actionValue.isArray() || actionValue.toArray().size() > MaxActionsPerCall)
        return fail(error, QStringLiteral("action_count_limit"));

    QList<ActionRequest> validatedActions;
    for (const QJsonValue &item : actionValue.toArray()) {
        if (!item.isObject())
            return fail(error, QStringLiteral("invalid_action"));

        const QJsonObject action = item.toObject();
        if (action.value(QStringLiteral("type")).toString() !=
            QStringLiteral("send_danmaku")) {
            return fail(error, QStringLiteral("unknown_action"));
        }

        const QString text = action.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty() || text.size() > 180)
            return fail(error, QStringLiteral("invalid_action_text"));

        validatedActions.append({QStringLiteral("send_danmaku"), text});
    }

    if (actions)
        *actions = validatedActions;

    return true;
}

} // namespace PluginContract
