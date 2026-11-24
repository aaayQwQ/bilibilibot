#include "bilibili.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QRandomGenerator>
#include <QMap>
#include <zlib.h>

namespace {
const int kHeaderSize = 16;
const QByteArray kMixinOrder = QByteArray::fromHex(
    "2e2f1202350817200f320a1f3a032d231b2b053121092a131d1c0e270c26290d2507300810373018");
}

BilibiliClient::BilibiliClient(qint64 roomId, QString cookie, QString csrf, QObject *parent)
    : QObject(parent), requestedRoomId_(roomId), cookie_(std::move(cookie)), csrf_(std::move(csrf)) {
    heartbeat_.setInterval(30000);
    reconnect_.setInterval(5000);
    reconnect_.setSingleShot(true);
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
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &BilibiliClient::parsePackets);
    connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit log("WebSocket 错误：" + socket_.errorString());
    });
}

void BilibiliClient::start() {
    stopped_ = false;
    beginLogin();
}

void BilibiliClient::stop() {
    stopped_ = true;
    heartbeat_.stop();
    reconnect_.stop();
    socket_.close();
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
        if (imgKey_.isEmpty() || subKey_.isEmpty()) {
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
    const QUrl url(QString("wss://%1:%2/sub").arg(host).arg(port));
    emit log("正在连接弹幕服务器：" + host);
    socket_.open(url);
}

void BilibiliClient::requestJson(const QUrl &url, JsonCallback callback) {
    QNetworkRequest request(url);
    request.setRawHeader("Cookie", cookie_.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 BilibiliBot/0.1");
    request.setRawHeader("Referer", "https://live.bilibili.com/");
    QNetworkReply *reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            connecting_ = false;
            emit log("Bilibili API 网络错误：" + reply->errorString());
            if (!stopped_) reconnect_.start();
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

void BilibiliClient::parsePackets(const QByteArray &data) {
    int offset = 0;
    while (offset + kHeaderSize <= data.size()) {
        const quint32 packetLength = readU32(data.constData() + offset);
        const quint16 headerLength = readU16(data.constData() + offset + 4);
        if (packetLength < kHeaderSize || headerLength < kHeaderSize ||
            packetLength > quint32(data.size() - offset)) break;
        const QByteArray packet = data.mid(offset, int(packetLength));
        parseOnePacket(packet);
        offset += int(packetLength);
    }
}

void BilibiliClient::parseOnePacket(const QByteArray &packet) {
    if (packet.size() < kHeaderSize) return;
    const quint16 version = readU16(packet.constData() + 6);
    const quint32 operation = readU32(packet.constData() + 8);
    const QByteArray body = packet.mid(kHeaderSize);
    if (operation == 8) {
        QJsonParseError e{};
        const auto doc = QJsonDocument::fromJson(body, &e);
        if (e.error == QJsonParseError::NoError && doc.object().value("code").toInt(-1) == 0) {
            heartbeat_.start();
            emit connected();
            emit log("直播间鉴权成功，开始接收弹幕。");
        } else {
            emit log("直播间鉴权失败：" + QString::fromUtf8(body));
            socket_.close();
        }
        return;
    }
    if (operation != 5) return;
    if (version == 2) {
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(body.constData()));
        stream.avail_in = uInt(body.size());
        if (inflateInit(&stream) != Z_OK) return;
        QByteArray output;
        char buffer[16384];
        int result = Z_OK;
        while (result == Z_OK) {
            stream.next_out = reinterpret_cast<Bytef *>(buffer);
            stream.avail_out = sizeof(buffer);
            result = inflate(&stream, Z_NO_FLUSH);
            const int produced = int(sizeof(buffer) - stream.avail_out);
            if (produced > 0) output.append(buffer, produced);
            if (output.size() > 8 * 1024 * 1024) { result = Z_DATA_ERROR; break; }
        }
        inflateEnd(&stream);
        if (result == Z_STREAM_END) parsePackets(output);
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
    if (!cmd.startsWith("DANMU_MSG")) return;
    const QJsonArray info = object.value("info").toArray();
    if (info.size() < 3) return;
    const QString text = info.at(1).toString().trimmed();
    const QJsonArray user = info.at(2).toArray();
    if (text.isEmpty() || user.size() < 2) return;
    const qint64 uid = user.at(0).toVariant().toLongLong();
    const QString name = user.at(1).toString();
    emit danmaku(uid, name, text);
}

void BilibiliClient::sendDanmaku(const QString &text) {
    if (text.trimmed().isEmpty() || csrf_.isEmpty()) return;
    QUrl url("https://api.live.bilibili.com/msg/send");
    QNetworkRequest request(url);
    request.setRawHeader("Cookie", cookie_.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 BilibiliBot/0.1");
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
    QNetworkReply *reply = network_.post(request, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            emit log("发送弹幕网络错误：" + reply->errorString());
            return;
        }
        QJsonParseError e{};
        const auto doc = QJsonDocument::fromJson(data, &e);
        if (e.error != QJsonParseError::NoError || doc.object().value("code").toInt(-1) != 0)
            emit log("发送弹幕失败：" + QString::fromUtf8(data.left(600)));
        else
            emit log("AI 回复已发送。");
    });
}
