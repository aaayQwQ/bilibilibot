#include "config.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSettings>
#include <QRegularExpression>
#include <QUrl>
#include <QHostAddress>

namespace {
QMap<QString, QString> parseCookieFields(QString raw) {
    raw = raw.trimmed();
    if (!raw.isEmpty() && raw.front() == QChar(0xFEFF))
        raw.remove(0, 1);

    if (raw.startsWith("Cookie:", Qt::CaseInsensitive))
        raw = raw.mid(raw.indexOf(':') + 1).trimmed();
    if (raw.startsWith("cookie=", Qt::CaseInsensitive) &&
        raw.contains("SESSDATA=", Qt::CaseInsensitive))
        raw = raw.mid(raw.indexOf('=') + 1).trimmed();

    QMap<QString, QString> fields;
    const QStringList parts = raw.split(';', Qt::KeepEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty())
            continue;
        const qsizetype equal = part.indexOf('=');
        if (equal <= 0)
            continue;

        QString key = part.left(equal).trimmed();
        const qsizetype colon = key.lastIndexOf(':');
        if (colon >= 0)
            key = key.mid(colon + 1).trimmed();
        const QString value = part.mid(equal + 1).trimmed();
        if (!key.isEmpty() && !value.isEmpty())
            fields.insert(key.toUpper(), value);
    }
    return fields;
}

// Supports the Netscape cookies.txt format exported by "Get cookies.txt LOCALLY".
// Plain Cookie headers are accepted as a convenience, too.
QString readCookieFile(const QString &configPath) {
    const QString cookiePath = QFileInfo(configPath).dir().filePath("cookie.txt");
    const QFileInfo cookieInfo(cookiePath);
    if (cookieInfo.isSymLink() || !cookieInfo.isFile() || cookieInfo.size() > 1024 * 1024)
        return {};
    QFile file(cookiePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QString raw = QString::fromUtf8(file.readAll()).trimmed();
    if (raw.isEmpty())
        return {};

    QMap<QString, QString> bilibiliCookies;
    const QStringList lines = raw.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        bool httpOnly = false;
        if (line.startsWith("#HttpOnly_", Qt::CaseInsensitive)) {
            line.remove(0, 10);
            httpOnly = true;
        } else if (line.startsWith('#')) {
            continue;
        }

        const QStringList columns = line.split('\t', Qt::KeepEmptyParts);
        if (columns.size() < 7)
            continue;

        QString domain = columns.at(0).trimmed().toLower();
        if (domain.startsWith('.'))
            domain.remove(0, 1);
        if (domain != "bilibili.com" && !domain.endsWith(".bilibili.com"))
            continue;

        bool expiryOk = false;
        const qint64 expiry = columns.at(4).toLongLong(&expiryOk);
        if (expiryOk && expiry > 0 && expiry <= now)
            continue;

        const QString name = columns.at(5).trimmed();
        const QString value = columns.at(6).trimmed();
        if (!name.isEmpty() && !value.isEmpty())
            bilibiliCookies.insert(name, value);
        Q_UNUSED(httpOnly);
    }

    if (!bilibiliCookies.isEmpty()) {
        QStringList headerParts;
        for (auto it = bilibiliCookies.cbegin(); it != bilibiliCookies.cend(); ++it)
            headerParts.push_back(it.key() + "=" + it.value());
        return headerParts.join("; ");
    }

    // Backward compatibility: accept a raw Cookie header stored in cookie.txt.
    return raw;
}
}

bool Config::load(const QString &path, QString *error) {
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("找不到配置文件：%1").arg(QFileInfo(path).absoluteFilePath());
        return false;
    }

    QSettings s(path, QSettings::IniFormat);
    roomId = s.value("bilibili/room_id", 0).toLongLong();
    // Cookie is deliberately kept out of config.ini. Load it from cookie.txt.
    cookie = readCookieFile(path);
    const QMap<QString, QString> cookieFields = parseCookieFields(cookie);

    aiProtocol = s.value("ai/protocol", "anthropic").toString().trimmed().toLower();
    aiBaseUrl = s.value("ai/base_url").toString().trimmed();
    aiApiKey = s.value("ai/api_key").toString().trimmed();
    aiModel = s.value("ai/model").toString().trimmed();
    aiAllowedHosts.clear();
    for (const QString &host : s.value("ai/allowed_hosts").toString().split(';', Qt::SkipEmptyParts)) {
        const QString normalized = host.trimmed().toLower();
        if (!normalized.isEmpty() && !aiAllowedHosts.contains(normalized))
            aiAllowedHosts.push_back(normalized);
    }
    thinking = s.value("ai/thinking", false).toBool();
    maxTokens = qBound(64, s.value("ai/max_tokens", 512).toInt(), 8192);
    systemPrompt = s.value("ai/system_prompt",
        "你是一个直播间聊天助手。请用简短、自然、友好的中文回答观众的问题。回答适合直播弹幕，不要使用 Markdown。").toString();

    keywords.clear();
    const QString rawKeywords = s.value("bot/keywords", "@AI").toString();
    for (const QString &word : rawKeywords.split('|', Qt::SkipEmptyParts)) {
        const QString w = word.trimmed();
        if (!w.isEmpty()) keywords.push_back(w);
    }

    maxReplyChars = qBound(20, s.value("bot/max_reply_chars", 180).toInt(), 180);
    userCooldownSeconds = qBound(0, s.value("bot/user_cooldown_seconds", 15).toInt(), 3600);
    roomCooldownSeconds = qBound(0, s.value("bot/room_cooldown_seconds", 3).toInt(), 3600);
    replyToSelf = s.value("bot/reply_to_self", true).toBool();
    memoryTurns = qBound(0, s.value("bot/memory_turns", 4).toInt(), 12);
    pluginsEnabled = s.value("plugins/enabled", false).toBool();
    pluginSendDanmakuEnabled = s.value("plugins/allow_send_danmaku", false).toBool();

    if (s.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("配置文件格式错误或无法读取：%1").arg(QFileInfo(path).absoluteFilePath());
        return false;
    }
    if (roomId <= 0) {
        if (error) *error = "bilibili.room_id 无效，请填写大于 0 的直播间号。";
        return false;
    }

    if (cookie.isEmpty()) {
        if (error) *error = "找不到有效的 cookie.txt。请将 Get cookies.txt LOCALLY 导出的 Netscape 格式文件放到 config.ini 同目录，并命名为 cookie.txt。";
        return false;
    }
    if (cookieFields.value("SESSDATA").isEmpty()) {
        if (error) *error = "cookie.txt 中没有找到 Bilibili 的 SESSDATA。请确认导出时包含 bilibili.com 域名的 Cookie。";
        return false;
    }
    csrf = cookieFields.value("BILI_JCT");
    if (csrf.isEmpty()) {
        if (error) *error = "cookie.txt 中没有找到 Bilibili 的 bili_jct。请确认导出的 Cookie 未过期且包含 bili_jct。";
        return false;
    }

    if (aiProtocol != "openai" && aiProtocol != "anthropic") {
        if (error) *error = "ai.protocol 只支持 openai 或 anthropic。";
        return false;
    }
    if (aiBaseUrl.isEmpty() || aiApiKey.isEmpty() || aiModel.isEmpty()) {
        if (error) *error = "请填写 ai.base_url、ai.api_key 和 ai.model。";
        return false;
    }
    const QUrl aiUrl(aiBaseUrl);
    QHostAddress literalAddress;
    const QString aiHost = aiUrl.host().toLower();
    const bool localHost = aiHost == "localhost" || aiHost.endsWith(".localhost") ||
        aiHost.endsWith(".local") || aiHost.endsWith(".internal") ||
        aiHost == "metadata.google.internal";
    static const QRegularExpression publicDnsName(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*\\.[a-z]{2,63}$"));
    if (!aiUrl.isValid() || aiUrl.scheme() != "https" || aiHost.isEmpty() ||
        !aiUrl.userInfo().isEmpty() || !aiUrl.query().isEmpty() || !aiUrl.fragment().isEmpty() ||
        (aiUrl.port(-1) != -1 && aiUrl.port(-1) != 443) ||
        !publicDnsName.match(aiHost).hasMatch() || literalAddress.setAddress(aiHost) || localHost) {
        if (error) *error = "ai.base_url 必须是 HTTPS 公网域名，禁止 IP 字面量、本地域名、用户信息、查询参数和片段。";
        return false;
    }
    if (!aiAllowedHosts.contains(aiHost)) {
        if (error) *error = QString("AI 主机 %1 不在 ai.allowed_hosts 显式白名单中。").arg(aiHost);
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::setPermissions(QFileInfo(path).dir().filePath("cookie.txt"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (keywords.isEmpty()) {
        if (error) *error = "bot.keywords 至少需要配置一个关键词。";
        return false;
    }
    return true;
}
