#include "transcription_history_store.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {
constexpr int kMaxEntries = 10;
}

TranscriptionHistoryStore::TranscriptionHistoryStore(QObject *parent)
    : QObject(parent) {
    load();
}

const QVector<TranscriptionHistoryEntry> &TranscriptionHistoryStore::entries() const {
    return m_entries;
}

QString TranscriptionHistoryStore::historyPath() const {
    return QDir::home().filePath(".config/my-whisper/history.json");
}

void TranscriptionHistoryStore::add(const QString &text) {
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) {
        return;
    }

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->text == cleaned) {
            m_entries.erase(it);
            break;
        }
    }

    m_entries.prepend({QUuid::createUuid().toString(QUuid::WithoutBraces), cleaned, QDateTime::currentDateTime()});
    while (m_entries.size() > kMaxEntries) {
        m_entries.removeLast();
    }

    save();
    emit entriesChanged();
}

void TranscriptionHistoryStore::select(const QString &entryId) {
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries[index].id != entryId) {
            continue;
        }

        QGuiApplication::clipboard()->setText(m_entries[index].text);
        auto selected = m_entries[index];
        selected.timestamp = QDateTime::currentDateTime();
        m_entries.removeAt(index);
        m_entries.prepend(selected);
        save();
        emit entriesChanged();
        return;
    }
}

void TranscriptionHistoryStore::load() {
    QFile file(historyPath());
    if (!file.open(QIODevice::ReadOnly)) {
        m_entries.clear();
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!document.isArray()) {
        m_entries.clear();
        return;
    }

    m_entries.clear();
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        TranscriptionHistoryEntry entry;
        entry.id = object.value("id").toString();
        entry.text = object.value("text").toString();
        entry.timestamp = QDateTime::fromString(object.value("timestamp").toString(), Qt::ISODate);
        if (entry.timestamp.isValid() && !entry.text.trimmed().isEmpty()) {
            m_entries.append(entry);
        }
        if (m_entries.size() >= kMaxEntries) {
            break;
        }
    }
}

void TranscriptionHistoryStore::save() {
    const QString path = historyPath();
    QDir().mkpath(QFileInfo(path).dir().absolutePath());

    QJsonArray array;
    for (const auto &entry : m_entries) {
        QJsonObject object;
        object["id"] = entry.id;
        object["text"] = entry.text;
        object["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        array.append(object);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.commit();
}
