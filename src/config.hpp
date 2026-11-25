#pragma once
#include <QString>
#include <QStringList>

struct Config {
    qint64 roomId = 0;
    QString cookie;
    QString csrf;
    QString aiProtocol = "anthropic";
    QString aiBaseUrl;
    QString aiApiKey;
    QString aiModel;
    QStringList aiAllowedHosts;
    QString systemPrompt;
    QStringList keywords;
    bool thinking = false;
    bool replyToSelf = true;
    int maxTokens = 512;
    int maxReplyChars = 180;
    int userCooldownSeconds = 15;
    int roomCooldownSeconds = 3;
    int memoryTurns = 4;
    bool pluginsEnabled = false;
    bool pluginSendDanmakuEnabled = false;
    bool logAiContent = false;
    bool redactAiContent = true;

    bool load(const QString &path, QString *error);
};
