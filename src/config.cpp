#include "config.h"
#include <QFileInfo>
#include <QSettings>

bool Config::load(const QString &path, QString *error) {
    QSettings s(path, QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    roomId = s.value("bilibili/room_id", 0).toLongLong();
    cookie = s.value("bilibili/cookie").toString().trimmed();
    aiBaseUrl = s.value("ai/base_url").toString().trimmed();
    aiApiKey = s.value("ai/api_key").toString().trimmed();
    aiModel = s.value("ai/model").toString().trimmed();
    thinking = s.value("ai/thinking", false).toBool();
    maxTokens = qBound(64, s.value("ai/max_tokens", 512).toInt(), 8192);
    systemPrompt = s.value("ai/system_prompt",
        "你是一个直播间聊天助手。请用简短、自然、友好的中文回答观众的问题。回答适合直播弹幕，不要使用 Markdown。").toString();
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

    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("找不到配置文件：%1（请复制 config.ini.example 为 config.ini）").arg(path);
        return false;
    }
    if (roomId <= 0 || cookie.isEmpty() || !cookie.contains("SESSDATA=") || !cookie.contains("bili_jct=")) {
        if (error) *error = "请填写有效的 room_id，并在 Cookie 中包含 SESSDATA 和 bili_jct。";
        return false;
    }
    const int csrfPos = cookie.indexOf("bili_jct=");
    if (csrfPos >= 0) {
        const int begin = csrfPos + 9;
        const int end = cookie.indexOf(';', begin);
        csrf = cookie.mid(begin, end < 0 ? -1 : end - begin).trimmed();
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
