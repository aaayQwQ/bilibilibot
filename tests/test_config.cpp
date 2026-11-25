#include "config.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>

class ConfigTest final : public QObject {
    Q_OBJECT
private:
    static bool writeFile(const QString &path, const QByteArray &data) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return file.write(data) == data.size();
    }
    static bool prepare(const QString &dir, const QByteArray &ini) {
        const QByteArray cookies =
            "www.bilibili.com\tTRUE\t/\tTRUE\t0\tSESSDATA\tfake-session\n"
            "www.bilibili.com\tTRUE\t/\tTRUE\t0\tbili_jct\tfake-csrf\n";
        return writeFile(QDir(dir).filePath("config.ini"), ini) &&
            writeFile(QDir(dir).filePath("cookie.txt"), cookies);
    }
private slots:
    void loadsNetscapeCookieAndExactHostAllowlist() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QByteArray ini =
            "[bilibili]\nroom_id=12345\n"
            "[ai]\nprotocol=openai\nbase_url=https://api.example.com\n"
            "allowed_hosts=api.example.com\napi_key=fake-key\nmodel=test-model\n"
            "[bot]\nkeywords=@AI\n";
        QVERIFY(prepare(dir.path(), ini));
        Config config; QString error;
        QVERIFY2(config.load(QDir(dir.path()).filePath("config.ini"), &error), qPrintable(error));
        QCOMPARE(config.roomId, qint64(12345));
        QVERIFY(config.cookie.contains("SESSDATA=fake-session"));
        QCOMPARE(config.aiAllowedHosts, QStringList{"api.example.com"});
    }
    void rejectsSymlinkedCookieFile() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QByteArray ini =
            "[bilibili]\nroom_id=1\n[ai]\nprotocol=openai\n"
            "base_url=https://api.example.com\nallowed_hosts=api.example.com\n"
            "api_key=x\nmodel=y\n[bot]\nkeywords=@AI\n";
        QVERIFY(writeFile(QDir(dir.path()).filePath("config.ini"), ini));
        const QString targetPath = QDir(dir.path()).filePath("cookie-target.txt");
        QVERIFY(writeFile(targetPath, "SESSDATA=fake; bili_jct=fake"));
        QFile target(targetPath);
        const QString cookiePath = QDir(dir.path()).filePath("cookie.txt");
        if (!target.link(cookiePath) || !QFileInfo(cookiePath).isSymLink())
            QSKIP("Symbolic links are unavailable in this test environment.");
        Config config; QString error;
        QVERIFY(!config.load(QDir(dir.path()).filePath("config.ini"), &error));
    }
    void rejectsHostNotInAllowlist() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QByteArray ini =
            "[bilibili]\nroom_id=1\n[ai]\nprotocol=openai\n"
            "base_url=https://api.example.com\nallowed_hosts=other.example.com\n"
            "api_key=x\nmodel=y\n[bot]\nkeywords=@AI\n";
        QVERIFY(prepare(dir.path(), ini));
        Config config; QString error;
        QVERIFY(!config.load(QDir(dir.path()).filePath("config.ini"), &error));
        QVERIFY(error.contains("白名单"));
    }
    void rejectsLoopbackAndIpLiteralEndpoints() {
        const QStringList urls{"https://127.0.0.1/v1", "https://localhost/v1", "https://2130706433/v1", "https://api.example.com:8443/v1"};
        for (const QString &url : urls) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            const QByteArray ini = QString(
                "[bilibili]\nroom_id=1\n[ai]\nprotocol=openai\nbase_url=%1\n"
                "allowed_hosts=localhost;127.0.0.1\napi_key=x\nmodel=y\n"
                "[bot]\nkeywords=@AI\n").arg(url).toUtf8();
            QVERIFY(prepare(dir.path(), ini));
            Config config; QString error;
            QVERIFY(!config.load(QDir(dir.path()).filePath("config.ini"), &error));
            QVERIFY(error.contains("公网域名"));
        }
    }
};
QTEST_GUILESS_MAIN(ConfigTest)
#include "test_config.moc"
