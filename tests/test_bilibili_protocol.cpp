#include "bilibili.hpp"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class BilibiliProtocolTest final : public QObject {
    Q_OBJECT
private slots:
    void rejectsTruncatedHeader() {
        BilibiliClient client(1, {}, {});
        QSignalSpy logs(&client, &BilibiliClient::log);
        client.parsePackets(QByteArray(7, '\0'));
        QVERIFY(!logs.isEmpty());
    }
    void rejectsLengthBeyondBuffer() {
        BilibiliClient client(1, {}, {});
        QByteArray packet(16, '\0');
        packet[0] = 0; packet[1] = 0; packet[2] = 0; packet[3] = 32;
        packet[4] = 0; packet[5] = 16;
        QSignalSpy logs(&client, &BilibiliClient::log);
        client.parsePackets(packet);
        QVERIFY(!logs.isEmpty());
    }
    void rejectsOutOfRangeEventNumbers() {
        BilibiliClient client(1, {}, {});
        QSignalSpy events(&client, &BilibiliClient::eventReceived);
        const QJsonObject command{{"cmd","SEND_GIFT"},{"data",QJsonObject{
            {"uid",1.0e300},{"uname","tester"},{"giftName","heart"},
            {"num",2},{"price",-1.0e300}}}};
        client.handleCommand(command);
        QCOMPARE(events.size(), 1);
        const QJsonObject event = qvariant_cast<QJsonObject>(events.first().first());
        QCOMPARE(event.value("uid").toInteger(), qint64(0));
        QCOMPARE(event.value("price").toInteger(), qint64(0));
    }
    void ignoresUnknownOpcode() {
        BilibiliClient client(1, {}, {});
        QSignalSpy events(&client, &BilibiliClient::eventReceived);
        client.parsePackets(BilibiliClient::makePacket(1, 999, QByteArray("ignored")));
        QCOMPARE(events.size(), 0);
    }
    void parsesConcatenatedPacketsWithinLimit() {
        BilibiliClient client(1, {}, {});
        QSignalSpy events(&client, &BilibiliClient::eventReceived);
        const QJsonObject command{{"cmd","SEND_GIFT"},{"data",QJsonObject{
            {"uid",123},{"uname","tester"},{"giftName","heart"},{"num",2}}}};
        const QByteArray body = QJsonDocument(command).toJson(QJsonDocument::Compact);
        const QByteArray packet = BilibiliClient::makePacket(1, 5, body);
        client.parsePackets(packet + packet);
        QCOMPARE(events.size(), 2);
    }
    void enforcesPacketCountLimit() {
        BilibiliClient client(1, {}, {});
        QSignalSpy logs(&client, &BilibiliClient::log);
        const QByteArray packet = BilibiliClient::makePacket(1, 999, {});
        QByteArray input;
        for (int i = 0; i < 4097; ++i) input += packet;
        client.parsePackets(input);
        QVERIFY(!logs.isEmpty());
    }
    void rejectsOversizedDecompressedPayload() {
        BilibiliClient client(1, {}, {});
        QSignalSpy events(&client, &BilibiliClient::eventReceived);
        const QByteArray oversized(8 * 1024 * 1024 + 1, 'x');
        const QByteArray compressed = qCompress(oversized).mid(4);
        client.parsePackets(BilibiliClient::makePacket(2, 5, compressed));
        QCOMPARE(events.size(), 0);
    }
};
QTEST_GUILESS_MAIN(BilibiliProtocolTest)
#include "test_bilibili_protocol.moc"
