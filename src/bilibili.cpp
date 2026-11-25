#include "bilibili.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QMap>
#include <QByteArray>
#include <QStringList>
#include <utility>

namespace {
const int kHeaderSize = 16;
QJsonValue firstField(const QJsonObject &object, const QStringList &keys) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (!value.isUndefined() && !value.isNull()) return value;
    }
    return {};
}
qint64 numericField(const QJsonObject &object, const QStringList &keys, qint64 fallback = 0) {
    const QJsonValue value = firstField(object, keys);
    if (value.isDouble()) return qint64(value.toDouble());
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
        if (ok) return parsed;
    }
    return fallback;
}
QString textField(const QJsonObject &object, const QStringList &keys) {
    const QJsonValue value = firstField(object, keys);
    return value.isString() ? value.toString() : QString();
}
}

BilibiliClient::BilibiliClient(qint64 roomId, QString cookie, QString csrf, QObject *parent)
    : QObject(parent), requestedRoomId_(roomId), cookie_(std::move(cookie)), csrf_(std::move(csrf)) {
    socket_.setMaxAllowedIncomingFrameSize(4 * 1024 * 1024);
    socket_.setMaxAllowedIncomingMessageSize(8 * 1024 * 1024);
    heartbeat_.setInterval(30000);
    reconnect_.setInterval(5000);
    reconnect_.setSingleShot(true);
    sendThrottle_.setSingleShot(true);
    sendThrottle_.setInterval(1000);
    connect(&sendThrottle_, &QTimer::timeout, this, &BilibiliClient::dispatchNextDanmaku);
    connect(&heartbeat_, &QTimer::timeout, this, [this]() {
        if (socket_.state() == QAbstractSocket::ConnectedState)
            sendPacket(1, 2, QByteArray());
    });
    connect(&reconnect_, &QTimer::timeout, this, &BilibiliClient::beginLogin);
    connect(&socket_, &QWebSocket::connected, this, [this]() {
        connecting_ = false;
        QJsonObject auth{
            {"uid", ownUid_},
            {"roomid", realRoomId_},
            {"protover", 2},
            {"platform", "web"},
            {"type", 2},
            {"key", danmuToken_}
        };
        sendPacket(1, 7, QJsonDocument(auth).toJson(QJsonDocument::Compact));
        emit log("WebSocket 已连接，正在发送直播间鉴权。");
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this]() {
        heartbeat_.stop();
        connecting_ = false;
        emit disconnected();
        if (!stopped_) {
            emit log("弹幕连接断开，5 秒后重连。");
            reconnect_.start();
        }
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        parsePackets(text.toUtf8());
    });
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &message) {
        parsePackets(message);
    });
    connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit log("WebSocket 错误：" + socket_.errorString());
    });
}

void BilibiliClient::start() {
    stopped_ = false;
    beginLogin();
}

void BilibiliClient::stop() {
    if (stopped_) return;
    stopped_ = true;
    connecting_ = false;
    heartbeat_.stop();
    reconnect_.stop();
    sendThrottle_.stop();
    sendQueue_.clear();
    sendBusy_ = false;

    // Detach callbacks before aborting replies: abort() emits finished(), and
    // shutdown must not schedule reconnects or invoke application callbacks.
    const auto replies = network_.findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
        if (!reply) continue;
        QObject::disconnect(reply, nullptr, this, nullptr);
        if (reply->isRunning()) reply->abort();
        reply->deleteLater();
    }
    socket_.abort();
}

void BilibiliClient::beginLogin() {
    if (stopped_ || connecting_) return;
    connecting_ = true;
    emit log("正在通过 Bilibili Cookie 验证账号……");
    requestJson(QUrl("https://api.bilibili.com/x/web-interface/nav"), [this](const QJsonObject &root) {
        const QJsonObject data = root.value("data").toObject();
        if (root.value("code").toInt(-1) != 0 || !data.value("isLogin").toBool()) {
            connecting_ = false;
            emit log("Cookie 登录失败或已过期，请检查 SESSDATA。");
            reconnect_.start();
            return;
        }
        ownUid_ = data.value("mid").toVariant().toLongLong();
        emit identityReady(ownUid_);
        const QJsonObject wbi = data.value("wbi_img").toObject();
        auto keyFromUrl = [](const QString &url) {
            const QString name = url.section('/', -1).section('.', 0, 0);
            return name;
        };
        imgKey_ = keyFromUrl(wbi.value("img_url").toString());
        subKey_ = keyFromUrl(wbi.value("sub_url").toString());
        static const QRegularExpression validWbiKey(QStringLiteral("^[A-Za-z0-9]{32}$"));
        if (!validWbiKey.match(imgKey_).hasMatch() || !validWbiKey.match(subKey_).hasMatch()) {
            connecting_ = false;
            emit log("无法从 Bilibili 获取 WBI 签名密钥。");
            reconnect_.start();
            return;
        }
        emit log(QString("Cookie 登录成功，账号 UID：%1").arg(ownUid_));
        loadRoomInfo();
    });
}

void BilibiliClient::loadRoomInfo() {
    QUrl url("https://api.live.bilibili.com/room/v1/Room/room_init");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(requestedRoomId_));
    url.setQuery(query);
    requestJson(url, [this](const QJsonObject &root) {
        const QJsonObject data = root.value("data").toObject();
        if (root.value("code").toInt(-1) != 0) {
            connecting_ = false;
            emit log("获取直播间信息失败：" + root.value("message").toString());
            reconnect_.start();
            return;
        }
        realRoomId_ = data.value("room_id").toVariant().toLongLong();
        if (realRoomId_ <= 0) realRoomId_ = requestedRoomId_;
        loadDanmuInfo();
    });
}

QString BilibiliClient::signedWbiQuery(const QMap<QString, QString> &input) const {
    static const int mixinIndex[] = {
        46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,
        27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13,
        37,48,7,16,24,55,40,61,26,17,0,1,60,51,30,4,
        22,25,54,21,56,59,6,63,57,62,11,36,20,34,44,52
    };
    const QByteArray raw = (imgKey_ + subKey_).toUtf8();
    QByteArray mixin;
    for (int i = 0; i < 32; ++i) {
        if (mixinIndex[i] < raw.size()) mixin.append(raw.at(mixinIndex[i]));
    }
    QMap<QString, QString> params = input;
    params.insert("wts", QString::number(QDateTime::currentSecsSinceEpoch()));
    QStringList pieces;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        QString value = it.value();
        value.remove(QRegularExpression("[!'()*]"));
        pieces << QString("%1=%2").arg(QString::fromLatin1(QUrl::toPercentEncoding(it.key())),
                                       QString::fromLatin1(QUrl::toPercentEncoding(value)));
    }
    const QByteArray query = pieces.join('&').toUtf8();
    const QByteArray signature = QCryptographicHash::hash(query + mixin, QCryptographicHash::Md5).toHex();
    return QString::fromUtf8(query) + "&w_rid=" + QString::fromLatin1(signature);
}

void BilibiliClient::loadDanmuInfo() {
    QMap<QString, QString> params{
        {"id", QString::number(realRoomId_)},
        {"type", "0"},
        {"web_location", "444.8"}
    };
    QUrl url("https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo");
    url.setQuery(signedWbiQuery(params));
    requestJson(url, [this](const QJsonObject &root) {
        if (root.value("code").toInt(-1) != 0) {
            connecting_ = false;
            emit log("获取弹幕服务器令牌失败：" + root.value("message").toString());
            reconnect_.start();
            return;
        }
        const QJsonObject data = root.value("data").toObject();
        danmuToken_ = data.value("token").toString();
        const QJsonArray hosts = data.value("host_list").toArray();
        if (danmuToken_.isEmpty() || hosts.isEmpty()) {
            connecting_ = false;
            emit log("弹幕服务器返回数据不完整。");
            reconnect_.start();
            return;
        }
        const QJsonObject host = hosts.first().toObject();
        connectSocket(host.value("host").toString(),
                      host.value("wss_port").toInt(443), danmuToken_);
    });
}

void BilibiliClient::connectSocket(const QString &host, int port, const QString &) {
    if (stopped_) return;
    const QString normalizedHost = host.trimmed().toLower();
    if ((!normalizedHost.endsWith(".bilibili.com") && normalizedHost != "bilibili.com") ||
        port <= 0 || port > 65535) {
        connecting_ = false;
        emit log("拒绝连接非 Bilibili 域名或无效端口。");
        reconnect_.start();
        return;
    }
    const QUrl url(QString("wss://%1:%2/sub").arg(normalizedHost).arg(port));
    emit log("正在连接弹幕服务器：" + host);
    socket_.open(url);
}

void BilibiliClient::requestJson(const QUrl &url, JsonCallback callback) {
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Cookie", cookie_.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 BilibiliBot/0.1");
    request.setRawHeader("Referer", "https://live.bilibili.com/");
    QNetworkReply *reply = network_.get(request);
    reply->setReadBufferSize(1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();

        if (stopped_) return;
        if (error != QNetworkReply::NoError) {
            connecting_ = false;
            emit log("Bilibili API 网络错误：" + errorString);
            reconnect_.start();
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            connecting_ = false;
            emit log("Bilibili API 返回了无效 JSON。");
            if (!stopped_) reconnect_.start();
            return;
        }
        callback(doc.object());
    });
}

quint32 BilibiliClient::readU32(const char *p) {
    return (quint32(quint8(p[0])) << 24) | (quint32(quint8(p[1])) << 16) |
           (quint32(quint8(p[2])) << 8) | quint32(quint8(p[3]));
}

quint16 BilibiliClient::readU16(const char *p) {
    return (quint16(quint8(p[0])) << 8) | quint16(quint8(p[1]));
}

QByteArray BilibiliClient::makePacket(quint16 version, quint32 operation, const QByteArray &body) {
    QByteArray out;
    const quint32 length = quint32(kHeaderSize + body.size());
    auto put16 = [&out](quint16 v) { out.append(char((v >> 8) & 0xff)); out.append(char(v & 0xff)); };
    auto put32 = [&out](quint32 v) {
        out.append(char((v >> 24) & 0xff)); out.append(char((v >> 16) & 0xff));
        out.append(char((v >> 8) & 0xff)); out.append(char(v & 0xff));
    };
    put32(length); put16(kHeaderSize); put16(version); put32(operation); put32(1);
    out += body;
    return out;
}

void BilibiliClient::sendPacket(quint16 version, quint32 operation, const QByteArray &body) {
    if (socket_.state() == QAbstractSocket::ConnectedState)
        socket_.sendBinaryMessage(makePacket(version, operation, body));
}

void BilibiliClient::parsePackets(const QByteArray &data, int depth) {
    constexpr int MaxTransportBytes = 8 * 1024 * 1024;
    constexpr int MaxPacketsPerMessage = 4096;
    constexpr int MaxNestingDepth = 2;

    if (data.isEmpty() || data.size() > MaxTransportBytes || depth > MaxNestingDepth) {
        emit log("弹幕数据超过安全解析限制，已丢弃。");
        return;
    }

    int offset = 0;
    int packetCount = 0;
    while (offset < data.size()) {
        if (data.size() - offset < kHeaderSize) {
            emit log("弹幕数据包头不完整，已丢弃剩余数据。");
            return;
        }
        if (++packetCount > MaxPacketsPerMessage) {
            emit log("单条 WebSocket 消息包含过多协议包，已停止解析。");
            return;
        }

        const quint32 packetLength = readU32(data.constData() + offset);
        const quint16 headerLength = readU16(data.constData() + offset + 4);
        if (packetLength < kHeaderSize || headerLength != kHeaderSize ||
            packetLength > quint32(MaxTransportBytes) ||
            packetLength > quint32(data.size() - offset)) {
            emit log("弹幕数据包长度或包头无效，已丢弃。");
            return;
        }

        parseOnePacket(data.mid(offset, int(packetLength)), depth);
        offset += int(packetLength);
    }
}

void BilibiliClient::parseOnePacket(const QByteArray &packet, int depth) {
    constexpr int MaxDecompressedBytes = 8 * 1024 * 1024;
    constexpr int MaxNestingDepth = 2;

    if (packet.size() < kHeaderSize) return;
    const quint16 version = readU16(packet.constData() + 6);
    const quint32 operation = readU32(packet.constData() + 8);
    const QByteArray body = packet.mid(kHeaderSize);

    if (operation == 8) {
        QJsonParseError e{};
        const auto doc = QJsonDocument::fromJson(body, &e);
        if (e.error == QJsonParseError::NoError && doc.isObject() &&
            doc.object().value("code").toInt(-1) == 0) {
            heartbeat_.start();
            emit connected();
            emit log("直播间鉴权成功，开始接收弹幕。");
        } else {
            emit log("直播间鉴权失败：认证响应无效或服务器拒绝连接。");
            socket_.close();
        }
        return;
    }

    if (operation != 5) return;

    if (version == 2) {
        if (depth >= MaxNestingDepth || body.size() < 6) {
            emit log("压缩弹幕包层级或长度超出限制，已拒绝处理。");
            return;
        }

        // Bilibili sends a standard zlib stream without Qt's four-byte
        // qCompress size prefix. qUncompress uses that prefix as its output
        // allocation ceiling, so prepend our own hard limit before decoding.
        QByteArray qtWrapped;
        qtWrapped.reserve(body.size() + 4);
        const quint32 limit = quint32(MaxDecompressedBytes);
        qtWrapped.append(char((limit >> 24) & 0xff));
        qtWrapped.append(char((limit >> 16) & 0xff));
        qtWrapped.append(char((limit >> 8) & 0xff));
        qtWrapped.append(char(limit & 0xff));
        qtWrapped += body;

        const QByteArray decompressed = qUncompress(qtWrapped);
        // Empty payloads are harmless; there is no nested packet to parse.
        if (decompressed.isEmpty()) return;
        parsePackets(decompressed, depth + 1);
        return;
    }

    if (version == 3) {
        emit log("服务器返回 Brotli 压缩数据，但客户端仅协商 zlib（protover=2），已拒绝。");
        return;
    }

    if (version == 0 || version == 1) {
        QJsonParseError e{};
        const auto doc = QJsonDocument::fromJson(body, &e);
        if (e.error == QJsonParseError::NoError && doc.isObject())
            handleCommand(doc.object());
    }
}

void BilibiliClient::handleCommand(const QJsonObject &object) {
    const QString cmd = object.value("cmd").toString();
    if (cmd.startsWith("DANMU_MSG")) {
        const QJsonArray info = object.value("info").toArray();
        if (info.size() < 3) return;
        const QString text = info.at(1).toString().trimmed();
        const QJsonArray user = info.at(2).toArray();
        if (text.isEmpty() || user.size() < 2) return;
        const qint64 uid = user.at(0).toVariant().toLongLong();
        const QString name = user.at(1).toString();
        emit danmaku(uid, name, text);
        emit eventReceived(QJsonObject{{"type","danmaku"},{"uid",uid},{"user",name},
            {"text",text},{"timestamp",QDateTime::currentSecsSinceEpoch()}});
        return;
    }
    const QJsonObject data = object.value("data").toObject();
    if (cmd == "SEND_GIFT") {
        emit eventReceived(QJsonObject{{"type","gift"},
            {"uid",numericField(data, {"uid", "user_id"})},
            {"user",textField(data, {"uname", "username", "user"})},
            {"gift_id",numericField(data, {"giftId", "gift_id", "giftid"})},
            {"gift",textField(data, {"giftName", "gift_name", "gift"})},
            {"count",qBound<qint64>(1, numericField(data, {"num", "count"}, 1), 1000000)},
            {"price",numericField(data, {"price", "unit_price"})},
            {"total_coin",numericField(data, {"total_coin", "totalCoin"})},
            {"coin_type",textField(data, {"coin_type", "coinType"})},
            {"timestamp",QDateTime::currentSecsSinceEpoch()}});
        return;
    }
    if (cmd == "SUPER_CHAT_MESSAGE") {
        const QJsonObject user = data.value("user_info").toObject();
        emit eventReceived(QJsonObject{{"type","superchat"},
            {"uid",numericField(user, {"uid", "user_id"})},
            {"user",textField(user, {"uname", "username", "user"})},
            {"message",data.value("message").toString()},
            {"price",numericField(data, {"price", "total_coin"})},
            {"message_id",numericField(data, {"id", "message_id"})},
            {"timestamp",QDateTime::currentSecsSinceEpoch()}});
        return;
    }
    if (cmd == "GUARD_BUY") {
        emit eventReceived(QJsonObject{{"type","guard_buy"},
            {"uid",numericField(data, {"uid", "user_id"})},
            {"user",textField(data, {"username", "uname", "user"})},
            {"guard_level",numericField(data, {"guard_level", "guardLevel"})},
            {"count",qBound<qint64>(1, numericField(data, {"num", "count"}, 1), 1000000)},
            {"price",numericField(data, {"price", "total_coin"})},
            {"timestamp",QDateTime::currentSecsSinceEpoch()}});
    }
}

void BilibiliClient::sendDanmaku(const QString &text, quint64 requestId) {
    QString clean;
    clean.reserve(qMin<qsizetype>(text.size(), 180));
    for (const QChar ch : text) {
        const auto category = ch.category();
        if (category == QChar::Other_Control || category == QChar::Other_Format ||
            category == QChar::Other_Surrogate || category == QChar::Other_NotAssigned)
            continue;
        clean.append(ch);
    }
    clean = clean.simplified();
    if (stopped_ || clean.isEmpty() || clean.size() > 180 || csrf_.isEmpty() || realRoomId_ <= 0) {
        emit danmakuSendFinished(requestId, clean, false, "弹幕为空、超长或客户端尚未就绪。");
        return;
    }
    if (sendQueue_.size() >= 30) {
        emit danmakuSendFinished(requestId, clean, false, "发送队列已满，已拒绝本次弹幕。");
        return;
    }
    sendQueue_.enqueue(qMakePair(requestId, clean));
    if (!sendBusy_ && !sendThrottle_.isActive()) dispatchNextDanmaku();
}

void BilibiliClient::dispatchNextDanmaku() {
    if (stopped_ || sendBusy_ || sendQueue_.isEmpty()) return;
    const auto item = sendQueue_.dequeue();
    const quint64 requestId = item.first;
    const QString text = item.second;
    QUrl url("https://api.live.bilibili.com/msg/send");
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Cookie", cookie_.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 BilibiliBot/1.0");
    request.setRawHeader("Referer", QString("https://live.bilibili.com/%1").arg(realRoomId_).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery form;
    form.addQueryItem("roomid", QString::number(realRoomId_));
    form.addQueryItem("msg", text);
    form.addQueryItem("color", "16777215");
    form.addQueryItem("fontsize", "25");
    form.addQueryItem("mode", "1");
    form.addQueryItem("rnd", QString::number(QDateTime::currentSecsSinceEpoch()));
    form.addQueryItem("csrf", csrf_);
    form.addQueryItem("csrf_token", csrf_);
    sendBusy_ = true;
    QNetworkReply *reply = network_.post(request, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, text]() {
        const auto error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray data = reply->isOpen() ? reply->readAll() : QByteArray();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        sendBusy_ = false;
        if (stopped_) return;
        bool success = error == QNetworkReply::NoError && status >= 200 && status < 300;
        QString reason;
        if (!success) {
            reason = error != QNetworkReply::NoError ? errorString : QString("HTTP %1").arg(status);
        } else {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            success = parseError.error == QJsonParseError::NoError && doc.isObject() &&
                doc.object().value("code").toInt(-1) == 0;
            if (!success) reason = QString::fromUtf8(data.left(600));
        }
        emit danmakuSendFinished(requestId, text, success, reason);
        if (success) emit log("弹幕发送成功。");
        else emit log("弹幕发送失败：" + reason);
        sendThrottle_.start();
    });
}
