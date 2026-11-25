#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QWebSocket>
#include <QTimer>
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QQueue>
#include <QPair>
#include <QUrl>
#include <functional>
#include <functional>

class BilibiliClient final : public QObject {
    Q_OBJECT
public:
    BilibiliClient(qint64 roomId, QString cookie, QString csrf, QObject *parent = nullptr);
    void start();
    void stop();
    void sendDanmaku(const QString &text, quint64 requestId = 0,
                     std::function<void(bool)> completion = {});
    qint64 ownUid() const { return ownUid_; }

signals:
    void log(const QString &message);
    void identityReady(qint64 uid);
    void danmaku(qint64 uid, const QString &name, const QString &text);
    void eventReceived(const QJsonObject &event);
    void connected();
    void disconnected();
    void danmakuSendFinished(quint64 requestId, const QString &text, bool success, const QString &reason);

private:
    friend class BilibiliProtocolTest;
    using JsonCallback = std::function<void(const QJsonObject &)>;
    void requestJson(const QUrl &url, JsonCallback callback);
    void beginLogin();
    void loadRoomInfo();
    void loadDanmuInfo();
    void connectSocket(const QString &host, int port, const QString &token);
    void sendPacket(quint16 version, quint32 operation, const QByteArray &body);
    void parsePackets(const QByteArray &data, int depth = 0);
    void parseOnePacket(const QByteArray &data, int depth);
    void handleCommand(const QJsonObject &object);
    void dispatchNextDanmaku();
    QString signedWbiQuery(const QMap<QString, QString> &params) const;
    static quint32 readU32(const char *p);
    static quint16 readU16(const char *p);
    static QByteArray makePacket(quint16 version, quint32 operation, const QByteArray &body);

    qint64 requestedRoomId_;
    qint64 realRoomId_ = 0;
    qint64 ownUid_ = 0;
    QString cookie_;
    QString csrf_;
    QString imgKey_;
    QString subKey_;
    QString danmuToken_;
    QNetworkAccessManager network_;
    QWebSocket socket_;
    QTimer heartbeat_;
    QTimer reconnect_;
    QTimer sendThrottle_;
    struct QueuedDanmaku {
        quint64 requestId = 0;
        QString text;
        std::function<void(bool)> completion;
    };
    QQueue<QueuedDanmaku> sendQueue_;
    bool sendBusy_ = false;
    bool stopped_ = true;
    bool connecting_ = false;
};
