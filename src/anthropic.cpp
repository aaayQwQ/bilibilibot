#include "anthropic.hpp"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

AIClient::AIClient(const Config &config, QObject *parent)
    : QObject(parent), config_(config) {}

void AIClient::stop() {
    if (stopped_) return;
    stopped_ = true;

    const auto replies = pendingReplies_.values();
    pendingReplies_.clear();
    for (QNetworkReply *reply : replies) {
        if (!reply) continue;
        QObject::disconnect(reply, nullptr, this, nullptr);
        if (reply->isRunning()) reply->abort();
        reply->deleteLater();
    }
}

void AIClient::generate(const QString &userName, const QString &message, const QJsonArray &history) {
    if (stopped_) {
        emit failed("AI 客户端已停止，拒绝提交新请求。");
        return;
    }

    QString base = config_.aiBaseUrl.trimmed();
    while (base.endsWith('/')) base.chop(1);

    const bool useOpenAI = config_.aiProtocol == "openai";
    QString endpoint;
    if (useOpenAI) {
        if (base.endsWith("/chat/completions"))
            endpoint = base;
        else if (base.endsWith("/v1"))
            endpoint = base + "/chat/completions";
        else
            endpoint = base + "/v1/chat/completions";
    } else {
        if (base.endsWith("/v1/messages"))
            endpoint = base;
        else if (base.endsWith("/v1"))
            endpoint = base + "/messages";
        else
            endpoint = base + "/v1/messages";
    }

    const QUrl url(endpoint);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty()) {
        emit failed("AI Base URL 无效：必须是无用户信息、无查询参数的 HTTPS 地址。");
        return;
    }

    QJsonArray messages;
    if (useOpenAI) {
        messages.append(QJsonObject{{"role", "system"}, {"content", config_.systemPrompt}});
        for (const QJsonValue &entry : history) messages.append(entry);
        messages.append(QJsonObject{{"role", "user"},
            {"content", QString("%1：%2").arg(userName, message)}});
    } else {
        messages = history;
        messages.append(QJsonObject{{"role", "user"},
            {"content", QString("%1：%2").arg(userName, message)}});
    }

    QJsonObject body;
    body.insert("model", config_.aiModel);
    body.insert("messages", messages);

    if (useOpenAI) {
        body.insert("max_tokens", config_.maxTokens);
        if (config_.thinking)
            body.insert("reasoning_effort", "medium");
    } else {
        body.insert("max_tokens", config_.maxTokens);
        body.insert("system", config_.systemPrompt);
        if (config_.thinking)
            body.insert("thinking", QJsonObject{{"type", "adaptive"}});
    }

    QNetworkRequest request(url);
    // Reasoning models may spend a long time before returning the first byte.
    // Transfer timeout is an inactivity timeout, not a total request deadline.
    request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("accept", "application/json");
    if (useOpenAI) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + config_.aiApiKey.toUtf8());
    } else {
        request.setRawHeader("x-api-key", config_.aiApiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
    }

    auto *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    pendingReplies_.insert(reply);
    QElapsedTimer elapsed;
    elapsed.start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, useOpenAI, url, elapsed]() mutable {
        const qint64 elapsedMs = elapsed.isValid() ? elapsed.elapsed() : -1;

        pendingReplies_.remove(reply);
        const auto netError = reply->error();
        const QString networkError = reply->errorString();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool deviceOpen = reply->isOpen();

        // QNetworkReply can be closed when an operation is aborted. readAll()
        // on that closed device produces "QIODevice::read: device not open".
        // Only consume the body while the reply is still open.
        QByteArray data;
        if (deviceOpen && reply->bytesAvailable() > 0)
            data = reply->readAll();

        reply->deleteLater();
        if (stopped_) return;

        if (netError != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString detail;
            if (netError == QNetworkReply::OperationCanceledError) {
                detail = QString("请求被取消（%1）；耗时 %2 ms；响应设备%3。")
                    .arg(networkError)
                    .arg(elapsedMs)
                    .arg(deviceOpen ? "仍打开" : "已关闭");
            } else if (!data.isEmpty()) {
                detail = QString::fromUtf8(data.left(1200));
            } else {
                detail = networkError;
            }

            emit failed(QString("%1 API 请求失败（HTTP %2，主机 %3）：%4")
                .arg(useOpenAI ? "OpenAI-compatible" : "Anthropic")
                .arg(status)
                .arg(url.host(), detail));
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit failed(QString("AI 返回了无法解析的 JSON（%1 字节）。").arg(data.size()));
            return;
        }

        QString result;
        if (useOpenAI) {
            const QJsonArray choices = doc.object().value("choices").toArray();
            if (!choices.isEmpty()) {
                const QJsonObject choice = choices.first().toObject();
                const QJsonValue content = choice.value("message").toObject().value("content");
                if (content.isString()) {
                    result = content.toString();
                } else if (content.isArray()) {
                    for (const QJsonValue &part : content.toArray()) {
                        const QJsonObject item = part.toObject();
                        if (item.value("type").toString() == "text")
                            result += item.value("text").toString();
                    }
                }
            }
        } else {
            const QJsonArray content = doc.object().value("content").toArray();
            for (const QJsonValue &v : content) {
                const QJsonObject block = v.toObject();
                if (block.value("type").toString() == "text")
                    result += block.value("text").toString();
            }
        }

        result = result.trimmed();
        if (result.isEmpty()) {
            emit failed("AI 没有返回文本内容（请检查模型、思考模式及服务商响应格式）。");
            return;
        }
        emit finished(result);
    });
}
