#pragma once

#include "types.h"

#include <QWidget>

class StatusOverlay : public QWidget {
    Q_OBJECT

public:
    explicit StatusOverlay(QWidget *parent = nullptr);

    void setState(AppState state, const QString &errorMessage = QString());
    void setAudioLevels(const QVector<float> &levels);
    void recenter();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QString stateLabel() const;

    AppState m_state = AppState::Idle;
    QString m_errorMessage;
    QVector<float> m_audioLevels;
};
