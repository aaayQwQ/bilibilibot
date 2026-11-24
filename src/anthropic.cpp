#include "anthropic.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

AnthropicClient::AnthropicClient(const Config &config, QObject *parent)
    : QObject(parent), config_(config) {}

void AnthropicClient::generate(const QString &userName, const QString &message, const QJsonArray &history) {
    QString base = config_.aiBaseUrl;
    while (base.endsWith('/')) base.chop(1);
    if (base.endsWith("/v1")) base.chop(3);
    const QUrl url(base + "/v1/messages");
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http")) {
        emit failed("AI Base URL 无效：必须是 http:// 或 https:// 地址。");
        return;
    }

    QJsonArray messages = history;
    messages.append(QJsonObject{{"role", "user"},
        {"content", QString("%1：%2").arg(userName, message)}});

    QJsonObject body{
        {"model", config_.aiModel},
        {"max_tokens", config_.maxTokens},
        {"system", config_.systemPrompt},
        {"messages", messages}
    };
    if (config_.thinking) {
        body.insert("thinking", QJsonObject{{"type", "adaptive"}});
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", config_.aiApiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("accept", "application/json");

    QNetworkReply *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto netError = reply->error();
        reply->deleteLater();
        if (netError != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit failed(QString("Anthropic API 请求失败（HTTP %1）：%2")
                .arg(status).arg(QString::fromUtf8(data.left(1200))));
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit failed("AI 返回了无法解析的 JSON。");
            return;
        }
        const QJsonArray content = doc.object().value("content").toArray();
        QString result;
        for (const QJsonValue &v : content) {
            const QJsonObject block = v.toObject();
            if (block.value("type").toString() == "text")
                result += block.value("text").toString();
        }
        result = result.trimmed();
        if (result.isEmpty()) {
            emit failed("AI 没有返回文本内容（可能只返回了思考块或其他内容）。");
            return;
        }
        emit finished(result);
    });
}
