#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QJsonArray>

namespace {
QJsonArray stringListToJson(const QStringList &values) {
    QJsonArray array;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            array.append(trimmed);
        }
    }
    return array;
}

QStringList stringListFromJson(const QJsonValue &value) {
    QStringList values;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            const QString trimmed = entry.toString().trimmed();
            if (!trimmed.isEmpty()) {
                values.append(trimmed);
            }
        }
    } else if (value.isString()) {
        const QString trimmed = value.toString().trimmed();
        if (!trimmed.isEmpty()) {
            values.append(trimmed);
        }
    }
    return values;
}

QJsonObject hotkeyToJson(const HotkeyBinding &binding) {
    QJsonObject object;
    object["keyCode"] = static_cast<int>(binding.keyCode);
    object["modifiers"] = static_cast<int>(binding.modifiers);
    return object;
}

HotkeyBinding hotkeyFromJson(const QJsonValue &value, const HotkeyBinding &fallback) {
    if (!value.isObject()) {
        return fallback;
    }

    const QJsonObject object = value.toObject();
    HotkeyBinding binding = fallback;
    binding.keyCode = static_cast<quint32>(object.value("keyCode").toInt(static_cast<int>(fallback.keyCode)));
    binding.modifiers = static_cast<quint32>(object.value("modifiers").toInt(static_cast<int>(fallback.modifiers)));
    return binding;
}

QString normalizedTranscriptionBackend(QString value) {
    value = value.trimmed().toLower();
    if (value == QStringLiteral("assemblyai")
        || value == QStringLiteral("assembly_ai")
        || value == QStringLiteral("assembly-ai")) {
        return QStringLiteral("assemblyai");
    }
    return QStringLiteral("deepgram");
}
}

QString Config::configPath() {
    return QDir::home().filePath(".config/my-whisper/config.json");
}

AppConfig Config::defaultConfig() {
    AppConfig config;
    config.transcriptionBackend = QStringLiteral("deepgram");
    config.deepgramApiKey = "YOUR_DEEPGRAM_API_KEY";
    config.deepgramKeywords.clear();
    config.assemblyAiApiKey = "YOUR_ASSEMBLYAI_API_KEY";
    config.assemblyAiSpeechModel = QStringLiteral("u3-rt-pro");
    config.openaiApiKey.clear();
    config.enableRefinement = false;
    config.refinementProvider = QStringLiteral("openai");
    config.refinementPrompt = "Fix spelling and grammar. Return only the fixed text.";
    config.llamaCppBinaryPath.clear();
    config.llamaCppAdditionalArgs.clear();
    config.llamaCppModelPath.clear();
    config.audioInputDeviceId.clear();
    config.toggleHotkey = HotkeyBinding::defaultToggle();
    config.abortHotkey = HotkeyBinding::defaultAbort();
    config.historyHotkey = HotkeyBinding::defaultHistory();
    config.showDoneScreen = false;
    return config;
}

AppConfig Config::load() {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        const AppConfig config = defaultConfig();
        save(config);
        return config;
    }

    const auto data = file.readAll();
    file.close();

    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        const AppConfig config = defaultConfig();
        save(config);
        return config;
    }

    const QJsonObject object = document.object();
    AppConfig config = defaultConfig();
    config.transcriptionBackend = normalizedTranscriptionBackend(object.value("transcriptionBackend").toString(config.transcriptionBackend));
    config.deepgramApiKey = object.value("deepgramApiKey").toString(config.deepgramApiKey);
    config.deepgramKeywords = stringListFromJson(object.value("deepgramKeywords"));
    config.assemblyAiApiKey = object.value("assemblyAiApiKey").toString(config.assemblyAiApiKey);
    config.assemblyAiSpeechModel = object.value("assemblyAiSpeechModel").toString(config.assemblyAiSpeechModel).trimmed();
    if (config.assemblyAiSpeechModel.isEmpty()) {
        config.assemblyAiSpeechModel = QStringLiteral("u3-rt-pro");
    }
    if (object.contains("openaiApiKey") && !object.value("openaiApiKey").isNull()) {
        config.openaiApiKey = object.value("openaiApiKey").toString();
    }
    config.enableRefinement = object.value("enableRefinement").toBool(false);
    config.refinementProvider = object.value("refinementProvider").toString(config.refinementProvider).trimmed().toLower();
    if (config.refinementProvider.isEmpty()) {
        config.refinementProvider = QStringLiteral("openai");
    }
    config.refinementPrompt = object.value("refinementPrompt").toString(config.refinementPrompt);
    if (object.contains("llamaCppBinaryPath") && !object.value("llamaCppBinaryPath").isNull()) {
        config.llamaCppBinaryPath = object.value("llamaCppBinaryPath").toString();
    }
    if (object.contains("llamaCppAdditionalArgs") && !object.value("llamaCppAdditionalArgs").isNull()) {
        config.llamaCppAdditionalArgs = object.value("llamaCppAdditionalArgs").toString();
    }
    if (object.contains("llamaCppModelPath") && !object.value("llamaCppModelPath").isNull()) {
        config.llamaCppModelPath = object.value("llamaCppModelPath").toString();
    }
    if (object.contains("audioInputDeviceId") && !object.value("audioInputDeviceId").isNull()) {
        config.audioInputDeviceId = object.value("audioInputDeviceId").toString();
    }
    config.toggleHotkey = hotkeyFromJson(object.value("toggleHotkey"), HotkeyBinding::defaultToggle());
    config.abortHotkey = hotkeyFromJson(object.value("abortHotkey"), HotkeyBinding::defaultAbort());
    config.historyHotkey = hotkeyFromJson(object.value("historyHotkey"), HotkeyBinding::defaultHistory());
    config.showDoneScreen = object.value("showDoneScreen").toBool(false);
    return config;
}

bool Config::save(const AppConfig &config, QString *errorMessage) {
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).dir().absolutePath());

    QJsonObject object;
    object["transcriptionBackend"] = normalizedTranscriptionBackend(config.transcriptionBackend);
    object["deepgramApiKey"] = config.deepgramApiKey;
    object["deepgramKeywords"] = stringListToJson(config.deepgramKeywords);
    object["assemblyAiApiKey"] = config.assemblyAiApiKey;
    object["assemblyAiSpeechModel"] = config.normalizedAssemblyAiSpeechModel();
    object["openaiApiKey"] = config.openaiApiKey.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(config.openaiApiKey);
    object["enableRefinement"] = config.enableRefinement;
    object["refinementProvider"] = config.refinementProvider;
    object["refinementPrompt"] = config.refinementPrompt;
    object["llamaCppBinaryPath"] = config.llamaCppBinaryPath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(config.llamaCppBinaryPath);
    object["llamaCppAdditionalArgs"] = config.llamaCppAdditionalArgs.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(config.llamaCppAdditionalArgs);
    object["llamaCppModelPath"] = config.llamaCppModelPath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(config.llamaCppModelPath);
    object["audioInputDeviceId"] = config.audioInputDeviceId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(config.audioInputDeviceId);
    object["toggleHotkey"] = hotkeyToJson(config.toggleHotkey);
    object["abortHotkey"] = hotkeyToJson(config.abortHotkey);
    object["historyHotkey"] = hotkeyToJson(config.historyHotkey);
    object["showDoneScreen"] = config.showDoneScreen;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QJsonDocument document(object);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}

bool Config::usesAssemblyAi(const AppConfig &config) {
    return normalizedTranscriptionBackend(config.transcriptionBackend) == QStringLiteral("assemblyai");
}

bool Config::hasValidDeepgramKey(const AppConfig &config) {
    return !config.deepgramApiKey.trimmed().isEmpty() && config.deepgramApiKey != "YOUR_DEEPGRAM_API_KEY";
}

bool Config::hasValidAssemblyAiKey(const AppConfig &config) {
    return !config.assemblyAiApiKey.trimmed().isEmpty() && config.assemblyAiApiKey != "YOUR_ASSEMBLYAI_API_KEY";
}

bool Config::hasUsableRefiner(const AppConfig &config) {
    if (!config.enableRefinement) {
        return false;
    }

    const QString provider = config.refinementProvider.trimmed().toLower();
    if (provider == QStringLiteral("llama_cpp")) {
        return !config.llamaCppModelPath.trimmed().isEmpty();
    }

    return !config.openaiApiKey.trimmed().isEmpty()
        && config.openaiApiKey != "YOUR_OPENAI_API_KEY";
}
