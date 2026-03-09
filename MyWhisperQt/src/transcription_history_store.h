#pragma once

#include "types.h"

#include <QObject>
#include <QVector>

class TranscriptionHistoryStore : public QObject {
    Q_OBJECT

public:
    explicit TranscriptionHistoryStore(QObject *parent = nullptr);

    const QVector<TranscriptionHistoryEntry> &entries() const;
    QString historyPath() const;

    void add(const QString &text);
    void select(const QString &entryId);

signals:
    void entriesChanged();

private:
    void load();
    void save();

    QVector<TranscriptionHistoryEntry> m_entries;
};
