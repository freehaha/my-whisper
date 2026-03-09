#pragma once

#include <QDialog>

class QListWidget;
class QListWidgetItem;
class QLabel;
class TranscriptionHistoryStore;

class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistoryDialog(TranscriptionHistoryStore *store, QWidget *parent = nullptr);
    void showAndFocus();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void refreshEntries();
    void activateSelection();

private:
    QString relativeTimestamp(const QDateTime &timestamp) const;
    QString previewText(const QString &text) const;
    void restoreSelection(const QString &preferredId);

    TranscriptionHistoryStore *m_store = nullptr;
    QLabel *m_titleLabel = nullptr;
    QListWidget *m_listWidget = nullptr;
    QLabel *m_footerLabel = nullptr;
};
