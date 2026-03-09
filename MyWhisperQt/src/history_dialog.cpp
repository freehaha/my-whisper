#include "history_dialog.h"
#include "transcription_history_store.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QVBoxLayout>

HistoryDialog::HistoryDialog(TranscriptionHistoryStore *store, QWidget *parent)
    : QDialog(parent)
    , m_store(store) {
    setWindowTitle(tr("Transcription History"));
    setModal(false);
    resize(520, 320);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_titleLabel = new QLabel(tr("Select a transcription to copy it to the clipboard"), this);
    m_listWidget = new QListWidget(this);
    m_footerLabel = new QLabel(tr("Use ↑/↓ to navigate, Enter to copy, Esc to close"), this);

    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setAlternatingRowColors(true);

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_listWidget, 1);
    layout->addWidget(m_footerLabel);

    connect(m_store, &TranscriptionHistoryStore::entriesChanged, this, &HistoryDialog::refreshEntries);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        activateSelection();
    });

    refreshEntries();
}

void HistoryDialog::showAndFocus() {
    refreshEntries();
    show();
    raise();
    activateWindow();
}

void HistoryDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateSelection();
        return;
    }

    QDialog::keyPressEvent(event);
}

void HistoryDialog::refreshEntries() {
    const QString selectedId = m_listWidget->currentItem() ? m_listWidget->currentItem()->data(Qt::UserRole).toString() : QString();

    m_listWidget->clear();
    const auto &entries = m_store->entries();
    if (entries.isEmpty()) {
        auto *item = new QListWidgetItem(tr("No transcription history yet."), m_listWidget);
        item->setFlags(Qt::NoItemFlags);
    } else {
        for (const auto &entry : entries) {
            const QString text = QStringLiteral("%1\n%2")
                .arg(previewText(entry.text), relativeTimestamp(entry.timestamp));
            auto *item = new QListWidgetItem(text, m_listWidget);
            item->setData(Qt::UserRole, entry.id);
            item->setToolTip(entry.text);
            item->setSizeHint(QSize(item->sizeHint().width(), 42));
        }
    }

    restoreSelection(selectedId);
}

void HistoryDialog::activateSelection() {
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return;
    }

    const QString entryId = item->data(Qt::UserRole).toString();
    if (entryId.isEmpty()) {
        return;
    }

    m_store->select(entryId);
    close();
}

QString HistoryDialog::relativeTimestamp(const QDateTime &timestamp) const {
    if (!timestamp.isValid()) {
        return tr("Unknown time");
    }

    const qint64 seconds = timestamp.secsTo(QDateTime::currentDateTime());
    if (seconds < 60) {
        return tr("just now");
    }
    if (seconds < 3600) {
        return tr("%1 min ago").arg(seconds / 60);
    }
    if (seconds < 86400) {
        return tr("%1 hr ago").arg(seconds / 3600);
    }
    return QLocale().toString(timestamp.date(), QLocale::ShortFormat);
}

QString HistoryDialog::previewText(const QString &text) const {
    const QString singleLine = text.simplified();
    return fontMetrics().elidedText(singleLine, Qt::ElideRight, m_listWidget->viewport()->width() - 24);
}

void HistoryDialog::restoreSelection(const QString &preferredId) {
    if (m_listWidget->count() == 0) {
        return;
    }

    if (!preferredId.isEmpty()) {
        for (int row = 0; row < m_listWidget->count(); ++row) {
            QListWidgetItem *item = m_listWidget->item(row);
            if (item->data(Qt::UserRole).toString() == preferredId) {
                m_listWidget->setCurrentItem(item);
                return;
            }
        }
    }

    if (m_listWidget->item(0)->flags() != Qt::NoItemFlags) {
        m_listWidget->setCurrentRow(0);
    }
}
