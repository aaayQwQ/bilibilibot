#include "config.h"
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSettings>

namespace {
QMap<QString, QString> parseCookieFields(QString raw) {
    raw = raw.trimmed();
    if (!raw.isEmpty() && raw.front() == QChar(0xFEFF))
        raw.remove(0, 1);

    // Accept a copied request header as well as a plain Cookie value.
    if (raw.startsWith("Cookie:", Qt::CaseInsensitive))
        raw = raw.mid(raw.indexOf(':') + 1).trimmed();

    // Also tolerate users pasting the literal "cookie=..." prefix.
    if (raw.startsWith("cookie=", Qt::CaseInsensitive) &&
        raw.contains("SESSDATA=", Qt::CaseInsensitive))
        raw = raw.mid(raw.indexOf('=') + 1).trimmed();

    // Some browser exports contain line breaks or a header label before the value.
    raw.replace("\r", "");
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
        // Ignore a leading header label if the export is "Cookie: KEY=value".
        const qsizetype colon = key.lastIndexOf(':');
        if (colon >= 0)
            key = key.mid(colon + 1).trimmed();
        key = key.toUpper();

        QString value = part.mid(equal + 1).trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);

        if (!key.isEmpty() && !value.isEmpty())
            fields.insert(key, value);
    }
    return fields;
}

QString readCookieFile(const QString &configPath) {
    const QString cookiePath = QFileInfo(configPath).dir().filePath("cookie.txt");
    QFile file(cookiePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    // Cookie files are expected to be UTF-8 text. Do not print or log their contents.
    return QString::fromUtf8(file.readAll()).trimmed();
}
}

bool Config::load(const QString &path, QString *error) {
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("找不到配置文件：%1（请复制 config.ini.example 为 config.ini）")
                                .arg(QFileInfo(path).absoluteFilePath());
        return false;
    }

    QSettings s(path, QSettings::IniFormat);
    roomId = s.value("bilibili/room_id", 0).toLongLong();
    cookie = s.value("bilibili/cookie").toString().trimmed();
    QMap<QString, QString> cookieFields = parseCookieFields(cookie);
    QString cookieSource = "config.ini";

    // If the INI value is empty or incomplete, automatically try cookie.txt
    // in the same directory. This also avoids failures when an INI editor
    // changes or truncates a long Cookie value.
    if (cookieFields.value("SESSDATA").isEmpty() ||
        cookieFields.value("BILI_JCT").isEmpty()) {
        const QString fileCookie = readCookieFile(path);
        const QMap<QString, QString> fileFields = parseCookieFields(fileCookie);
        if (!fileFields.value("SESSDATA").isEmpty() &&
            !fileFields.value("BILI_JCT").isEmpty()) {
            cookie = fileCookie;
            cookieFields = fileFields;
            cookieSource = "cookie.txt";
        }
    }

    aiProtocol = s.value("ai/protocol", "anthropic").toString().trimmed().toLower();
    aiBaseUrl = s.value("ai/base_url").toString().trimmed();
    aiApiKey = s.value("ai/api_key").toString().trimmed();
    aiModel = s.value("ai/model").toString().trimmed();
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

    maxReplyChars = qBound(20, s.value("bot/max_reply_chars", 180).toInt(), 500);
    userCooldownSeconds = qBound(0, s.value("bot/user_cooldown_seconds", 15).toInt(), 3600);
    roomCooldownSeconds = qBound(0, s.value("bot/room_cooldown_seconds", 3).toInt(), 3600);
    replyToSelf = s.value("bot/reply_to_self", true).toBool();
    memoryTurns = qBound(0, s.value("bot/memory_turns", 4).toInt(), 12);

    if (s.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("配置文件格式错误或无法读取：%1")
                                .arg(QFileInfo(path).absoluteFilePath());
        return false;
    }
    if (roomId <= 0) {
        if (error) *error = "bilibili.room_id 无效，请填写大于 0 的直播间号。";
        return false;
    }

    const QString sessdata = cookieFields.value("SESSDATA");
    csrf = cookieFields.value("BILI_JCT");
    if (sessdata.isEmpty()) {
        if (error) *error = "config.ini 和 cookie.txt 中都没有识别到 SESSDATA。请检查 Cookie 内容和格式。";
        return false;
    }
    if (csrf.isEmpty()) {
        if (error) *error = "Cookie 中没有识别到 bili_jct。请检查 Cookie 内容和格式。";
        return false;
    }

    // Keep the full cookie for authenticated Bilibili requests; only SESSDATA and
    // bili_jct are required for validation and CSRF extraction.
    Q_UNUSED(cookieSource);

    if (aiProtocol != "openai" && aiProtocol != "anthropic") {
        if (error) *error = "ai.protocol 只支持 openai 或 anthropic。";
        return false;
    }
    if (aiBaseUrl.isEmpty() || aiApiKey.isEmpty() || aiModel.isEmpty()) {
        if (error) *error = "请填写 ai.base_url、ai.api_key 和 ai.model。";
        return false;
    }
    if (keywords.isEmpty()) {
        if (error) *error = "bot.keywords 至少需要配置一个关键词。";
        return false;
    }
    return true;
}
