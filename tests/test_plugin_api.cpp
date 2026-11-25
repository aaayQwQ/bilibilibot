#include "plugin_api.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>

class PluginApiTest final : public QObject {
    Q_OBJECT
private:
    static bool writeManifest(const QString &dir, const QString &name, const QByteArray &json) {
        QFile file(QDir(dir).filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(json) == json.size();
    }
private slots:
    void giftRuleSupportsStringCount() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "gift.json",
            R"({"schema_version":1,"id":"gift-thanks","enabled":true,"event":"gift","min_gift_count":2,"cooldown_seconds":0,"action":{"type":"send_danmaku","template":"感谢 {user} 送出 {gift} x{count}"}})"));
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load(); QCOMPARE(manager.pluginCount(), 1);
        manager.dispatch(QJsonObject{{"type","gift"},{"user","Alice"},{"gift","小心心"},{"count","3"}});
        QCOMPARE(sent.size(), 1); QCOMPARE(sent.first(), QStringLiteral("感谢 Alice 送出 小心心 x3"));
    }
    void sendPermissionIsRequired() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QStringList sent;
        PluginManager manager(dir.path(), true, false, [&sent](const QString &s){ sent.push_back(s); });
        manager.dispatch(QJsonObject{{"type","gift"}});
        QVERIFY(sent.isEmpty());
    }
    void unresolvedPlaceholderIsRejected() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "unknown.json",
            R"({"schema_version":1,"id":"unknown","enabled":true,"event":"gift","cooldown_seconds":0,"action":{"type":"send_danmaku","template":"Hi {unknown}"}})"));
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load(); manager.dispatch(QJsonObject{{"type","gift"}});
        QVERIFY(sent.isEmpty());
    }
    void controlCharactersAreRemoved() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "safe.json",
            R"({"schema_version":1,"id":"safe","enabled":true,"event":"gift","cooldown_seconds":0,"action":{"type":"send_danmaku","template":"Hello {user}"}})"));
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load();
        manager.dispatch(QJsonObject{{"type","gift"},{"count",1},{"user",QStringLiteral("A\nB\u202e")}});
        QCOMPARE(sent.size(), 1);
        QVERIFY(!sent.first().contains('\n'));
        QVERIFY(!sent.first().contains(QChar(0x202e)));
    }
    void supplementaryUnicodeIsPreserved() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "unicode.json",
            R"({"schema_version":1,"id":"unicode","enabled":true,"event":"gift","cooldown_seconds":0,"action":{"type":"send_danmaku","template":"Hi {user}"}})"));
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load();
        const QString name = QString::fromUtf8("A😀𠀋👨‍👩‍👧‍👦");
        manager.dispatch(QJsonObject{{"type","gift"},{"count",1},{"user",name}});
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first(), QString("Hi ") + name);
    }
    void outOfRangeJsonNumbersAreRejected() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "range.json",
            R"({"schema_version":1,"id":"range","enabled":true,"event":"gift","min_gift_count":1,"cooldown_seconds":0,"action":{"type":"send_danmaku","template":"count={count}"}})"));
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load();
        manager.dispatch(QJsonObject{{"type","gift"},{"count",1.0e300}});
        manager.dispatch(QJsonObject{{"type","gift"},{"count",-1.0e300}});
        manager.dispatch(QJsonObject{{"type","gift"},{"count",9223372036854775808.0}});
        QVERIFY(sent.isEmpty());
    }
    void oversizedTemplateIsRejected() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString templateText(201, QLatin1Char('x'));
        const QByteArray json = QJsonDocument(QJsonObject{
            {"schema_version",1},{"id","long"},{"enabled",true},{"event","gift"},
            {"action",QJsonObject{{"type","send_danmaku"},{"template",templateText}}}
        }).toJson(QJsonDocument::Compact);
        QVERIFY(writeManifest(dir.path(), "long.json", json));
        PluginManager manager(dir.path(), true, true, [](const QString &) {});
        manager.load(); QCOMPARE(manager.pluginCount(), 0);
    }
    void cooldownBlocksImmediateRepeat() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "cooldown.json",
            R"({"schema_version":1,"id":"cooldown","enabled":true,"event":"gift","cooldown_seconds":30,"action":{"type":"send_danmaku","template":"thanks"}})"));
        QStringList sent;
        PluginManager manager(dir.path(), true, true, [&sent](const QString &s){ sent.push_back(s); });
        manager.load();
        const QJsonObject event{{"type","gift"},{"count",1}};
        manager.dispatch(event);
        QTest::qWait(3100); // Let the room-wide throttle expire; the per-plugin cooldown must still block.
        manager.dispatch(event);
        QCOMPARE(sent.size(), 1);
    }
    void malformedJsonIsIgnored() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "broken.json", "{not-json"));
        PluginManager manager(dir.path(), true, true, [](const QString &) {});
        manager.load(); QCOMPARE(manager.pluginCount(), 0);
    }
    void duplicateIdsAreRejected() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QByteArray json = R"({"schema_version":1,"id":"same","enabled":true,"event":"gift","cooldown_seconds":0,"action":{"type":"send_danmaku","template":"ok"}})";
        QVERIFY(writeManifest(dir.path(), "a.json", json));
        QVERIFY(writeManifest(dir.path(), "b.json", json));
        PluginManager manager(dir.path(), true, true, [](const QString &) {});
        manager.load(); QCOMPARE(manager.pluginCount(), 1);
    }
    void symbolicLinkManifestIsIgnored() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeManifest(dir.path(), "real.json",
            R"({"schema_version":1,"id":"real","enabled":true,"event":"gift","action":{"type":"send_danmaku","template":"ok"}})"));
        const QString target = QDir(dir.path()).filePath("real.json");
        const QString link = QDir(dir.path()).filePath("linked.json");
        QFile targetFile(target);
        if (!targetFile.link(link) || !QFileInfo(link).isSymLink())
            QSKIP("Symbolic links are unavailable in this test environment.");
        PluginManager manager(dir.path(), true, true, [](const QString &) {});
        manager.load(); QCOMPARE(manager.pluginCount(), 1);
    }
    void pluginCountIsBounded() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        for (int i=0;i<40;++i) {
            const QString id = QString("p%1").arg(i);
            const QByteArray json = QJsonDocument(QJsonObject{
                {"schema_version",1},{"id",id},{"enabled",true},{"event","gift"},
                {"action",QJsonObject{{"type","send_danmaku"},{"template","ok"}}}
            }).toJson(QJsonDocument::Compact);
            QVERIFY(writeManifest(dir.path(), id + ".json", json));
        }
        PluginManager manager(dir.path(), true, true, [](const QString &) {});
        manager.load(); QCOMPARE(manager.pluginCount(), 32);
    }
};
QTEST_GUILESS_MAIN(PluginApiTest)
#include "test_plugin_api.moc"
