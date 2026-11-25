#include "anthropic.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

AIClient::AIClient(const Config &config, QObject *parent)
    : QObject(parent), config_(config) {}

void AIClient::generate(const QString &userName, const QString &message, const QJsonArray &history) {
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
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()) {
        emit failed("AI Base URL 无效：必须是有效的 http:// 或 https:// 地址。");
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
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("accept", "application/json");
    if (useOpenAI) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + config_.aiApiKey.toUtf8());
    } else {
        request.setRawHeader("x-api-key", config_.aiApiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
    }

    QNetworkReply *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, useOpenAI]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto netError = reply->error();
        const QString networkError = reply->errorString();
        reply->deleteLater();

        if (netError != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString detail = QString::fromUtf8(data.left(1200));
            if (detail.isEmpty()) detail = networkError;
            emit failed(QString("%1 API 请求失败（HTTP %2）：%3")
                .arg(useOpenAI ? "OpenAI-compatible" : "Anthropic")
                .arg(status).arg(detail));
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit failed("AI 返回了无法解析的 JSON。");
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
