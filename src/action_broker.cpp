#include "action_broker.hpp"

#include <QPointer>

#include <atomic>
#include <memory>
#include <utility>

ActionBroker::ActionBroker(Policy policy, Sender sender, QObject *parent)
    : QObject(parent),
      policy_(std::move(policy)),
      sender_(std::move(sender)) {
    clock_.start();
}

QString ActionBroker::errorName(Error error) {
    switch (error) {
    case Error::None:
        return "none";
    case Error::Disabled:
        return "disabled";
    case Error::Unauthorized:
        return "unauthorized";
    case Error::InvalidAction:
        return "invalid_action";
    case Error::InvalidText:
        return "invalid_text";
    case Error::TooLong:
        return "too_long";
    case Error::Busy:
        return "busy";
    case Error::RateLimited:
        return "rate_limited";
    case Error::SendFailed:
        return "send_failed";
    }

    return "unknown";
}

ActionBroker::Result ActionBroker::submit(
    const QString &pluginId,
    const QString &action,
    const QString &text,
    std::function<void(bool)> completion) {
    const auto reject = [](Error error) {
        return Result{error, false};
    };

    if (!policy_.enabled || !policy_.allowSendDanmaku || !sender_)
        return reject(Error::Disabled);

    if (pluginId.isEmpty() ||
        (!policy_.allowedPluginIds.contains(pluginId) &&
         !policy_.allowedPluginIds.contains("*"))) {
        return reject(Error::Unauthorized);
    }

    if (action != "send_danmaku")
        return reject(Error::InvalidAction);

    if (text.trimmed().isEmpty())
        return reject(Error::InvalidText);

    QString clean;
    clean.reserve(text.size());

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);

        if (character.isHighSurrogate() &&
            i + 1 < text.size() &&
            text.at(i + 1).isLowSurrogate()) {
            clean += character;
            clean += text.at(++i);
            continue;
        }

        const auto category = character.category();
        if (category == QChar::Other_Control ||
            (category == QChar::Other_Format && character.unicode() != 0x200D) ||
            category == QChar::Other_Surrogate ||
            category == QChar::Other_NotAssigned) {
            continue;
        }

        clean += character;
    }

    clean = clean.simplified();
    if (clean.isEmpty())
        return reject(Error::InvalidText);

    if (clean.size() > policy_.maxMessageLength)
        return reject(Error::TooLong);

    if (inFlight_)
        return reject(Error::Busy);

    if (lastSuccessMs_ >= 0 &&
        clock_.elapsed() - lastSuccessMs_ < policy_.minIntervalMs) {
        return reject(Error::RateLimited);
    }

    inFlight_ = true;
    QPointer<ActionBroker> guard(this);
    const auto completed = std::make_shared<std::atomic_bool>(false);

    try {
        sender_(clean, [guard, completed, completion = std::move(completion)](bool success) {
            if (!guard || completed->exchange(true))
                return;

            guard->finish(success, completion);
        });
    } catch (...) {
        inFlight_ = false;
        return reject(Error::SendFailed);
    }

    return {Error::None, true};
}

void ActionBroker::finish(bool success,
                          const std::function<void(bool)> &completion) {
    if (!inFlight_)
        return;

    inFlight_ = false;
    if (success)
        lastSuccessMs_ = clock_.elapsed();

    if (completion)
        completion(success);
}
