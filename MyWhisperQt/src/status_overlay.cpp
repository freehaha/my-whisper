#include "status_overlay.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QShowEvent>

StatusOverlay::StatusOverlay(QWidget *parent)
    : QWidget(parent)
    , m_audioLevels(defaultAudioLevels()) {
    setFixedSize(280, 76);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void StatusOverlay::setState(AppState state, const QString &errorMessage) {
    m_state = state;
    m_errorMessage = errorMessage;

    if (m_state == AppState::Error) {
        setFixedSize(560, 190);
    } else {
        setFixedSize(280, 76);
    }

    update();
}

void StatusOverlay::setAudioLevels(const QVector<float> &levels) {
    m_audioLevels = levels;
    update();
}

void StatusOverlay::recenter() {
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center().x() - width() / 2, available.center().y() - height() / 2);
    }
}

void StatusOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 210));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 16, 16);

    const QRect contentRect = rect().adjusted(20, 16, -20, -16);
    const QRect singleLineIconRect(contentRect.left(), contentRect.top(), 36, contentRect.height());
    const QRect singleLineTextRect(contentRect.left() + 52, contentRect.top(), contentRect.width() - 52, contentRect.height());
    const QRect errorIconRect(contentRect.left(), contentRect.top(), 36, 30);
    const QRect errorTitleRect(contentRect.left() + 52, contentRect.top(), contentRect.width() - 52, 30);
    const QRect errorMessageRect(contentRect.left() + 52, contentRect.top() + 36, contentRect.width() - 52, contentRect.height() - 36);

    if (m_state == AppState::Recording) {
        painter.setBrush(QColor(220, 53, 69));
        painter.drawEllipse(QRectF(contentRect.left() + 4, contentRect.center().y() - 8, 16, 16));

        painter.setBrush(Qt::white);
        const int baseX = contentRect.left() + 52;
        for (int i = 0; i < m_audioLevels.size(); ++i) {
            const float level = m_audioLevels[i];
            const int barHeight = qMax(6, static_cast<int>(level * 28));
            const int x = baseX + i * 12;
            const int y = contentRect.center().y() - barHeight / 2;
            painter.drawRoundedRect(QRectF(x, y, 8, barHeight), 3, 3);
        }

        painter.setPen(Qt::white);
        painter.drawText(QRect(contentRect.left() + 130, contentRect.top(), contentRect.width() - 130, contentRect.height()),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         tr("Recording"));
        return;
    }

    QColor accent = QColor(255, 255, 255);
    QString symbol;
    QString label = stateLabel();

    switch (m_state) {
    case AppState::Transcribing:
        accent = QColor(76, 166, 255);
        symbol = QString::fromUtf8("✦");
        break;
    case AppState::Refining:
        accent = QColor(200, 120, 255);
        symbol = QString::fromUtf8("✧");
        break;
    case AppState::Done:
        accent = QColor(72, 187, 120);
        symbol = QString::fromUtf8("✓");
        break;
    case AppState::Error:
        accent = QColor(255, 208, 0);
        symbol = QString::fromUtf8("⚠");
        break;
    case AppState::Idle:
    case AppState::Recording:
        break;
    }

    painter.setPen(accent);
    QFont symbolFont = painter.font();
    symbolFont.setPointSize(22);
    symbolFont.setBold(true);
    painter.setFont(symbolFont);
    painter.drawText(m_state == AppState::Error ? errorIconRect : singleLineIconRect, Qt::AlignCenter, symbol);

    painter.setPen(Qt::white);
    QFont labelFont = painter.font();
    labelFont.setPointSize(13);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.drawText(m_state == AppState::Error ? errorTitleRect : singleLineTextRect, Qt::AlignLeft | Qt::AlignVCenter, label);

    if (m_state == AppState::Error && !m_errorMessage.isEmpty()) {
        QFont messageFont = painter.font();
        messageFont.setPointSize(10);
        messageFont.setBold(false);
        painter.setFont(messageFont);
        painter.setPen(QColor(230, 230, 230));
        painter.drawText(errorMessageRect,
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                         m_errorMessage);
    }
}

void StatusOverlay::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    recenter();
}

QString StatusOverlay::stateLabel() const {
    switch (m_state) {
    case AppState::Idle:
        return QString();
    case AppState::Recording:
        return tr("Recording");
    case AppState::Transcribing:
        return tr("Transcribing…");
    case AppState::Refining:
        return tr("Refining…");
    case AppState::Done:
        return tr("Done");
    case AppState::Error:
        return tr("Error");
    }
    return QString();
}
