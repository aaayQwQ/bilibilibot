#include "plugin_api.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
class PluginApiTest final : public QObject {
    Q_OBJECT
private slots:
    void giftRuleIsDispatched() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile file(dir.filePath("gift.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"schema_version":1,"id":"gift-thanks","enabled":true,"event":"gift","min_gift_count":2,"cooldown_seconds":0,"action":{"type":"send_danmaku","template":"感谢 {user} 送出 {gift} x{count}"}})");
        file.close();
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load(); QCOMPARE(manager.pluginCount(), 1);
        manager.dispatch(QJsonObject{{"type","gift"},{"user","Alice"},{"gift","小心心"},{"count",3}});
        QCOMPARE(sent.size(), 1); QCOMPARE(sent.first(), QStringLiteral("感谢 Alice 送出 小心心 x3"));
        manager.dispatch(QJsonObject{{"type","gift"},{"user","Bob"},{"gift","小心心"},{"count",1}});
        QCOMPARE(sent.size(), 1);
    }
    void sendPermissionIsRequired() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QStringList sent;
        PluginManager manager(dir.path(), true, false, [&sent](const QString &s){ sent.push_back(s); });
        manager.dispatch(QJsonObject{{"type","gift"}});
        QVERIFY(sent.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PluginApiTest)
#include "test_plugin_api.moc"
