#include "platform_integration.h"

#include <QApplication>
#include <QAudioOutput>
#include <QClipboard>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QUrl>

namespace {
class MediaCue {
public:
    MediaCue(QUrl source, float volume)
        : m_source(std::move(source))
        , m_volume(volume) {
    }

    void preload() {
        ensurePlayer();
    }

    void play() {
        ensurePlayer();
        m_playWhenReady = true;

        switch (m_player->mediaStatus()) {
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
        case QMediaPlayer::BufferingMedia:
        case QMediaPlayer::EndOfMedia:
            playNow();
            break;
        case QMediaPlayer::LoadingMedia:
        case QMediaPlayer::NoMedia:
        case QMediaPlayer::StalledMedia:
            break;
        case QMediaPlayer::InvalidMedia:
            qWarning() << "Invalid cue media, retrying:" << m_source;
            reload();
            break;
        }
    }

private:
    void ensurePlayer() {
        if (m_player) {
            return;
        }

        m_audioOutput = new QAudioOutput(qApp);
        m_audioOutput->setVolume(m_volume);

        m_player = new QMediaPlayer(qApp);
        m_player->setAudioOutput(m_audioOutput);

        QObject::connect(m_player, &QMediaPlayer::mediaStatusChanged, m_player, [this](QMediaPlayer::MediaStatus status) {
            switch (status) {
            case QMediaPlayer::LoadedMedia:
            case QMediaPlayer::BufferedMedia:
            case QMediaPlayer::BufferingMedia:
            case QMediaPlayer::EndOfMedia:
                if (m_playWhenReady) {
                    playNow();
                }
                break;
            case QMediaPlayer::InvalidMedia:
                qWarning() << "Unable to use cue media:" << m_source;
                if (m_playWhenReady) {
                    m_playWhenReady = false;
                    QApplication::beep();
                }
                break;
            case QMediaPlayer::NoMedia:
            case QMediaPlayer::LoadingMedia:
            case QMediaPlayer::StalledMedia:
                break;
            }
        });

        QObject::connect(m_player, &QMediaPlayer::errorOccurred, m_player, [this](QMediaPlayer::Error, const QString &errorString) {
            qWarning() << "Cue playback error:" << m_source << errorString;
            if (m_playWhenReady) {
                m_playWhenReady = false;
                QApplication::beep();
            }
        });

        m_player->setSource(m_source);
    }

    void reload() {
        ensurePlayer();
        m_player->stop();
        m_player->setSource(QUrl());
        m_player->setSource(m_source);
    }

    void playNow() {
        m_playWhenReady = false;
        m_player->stop();
        m_player->setPosition(0);
        m_player->play();
    }

    const QUrl m_source;
    const float m_volume = 0.7f;
    QAudioOutput *m_audioOutput = nullptr;
    QMediaPlayer *m_player = nullptr;
    bool m_playWhenReady = false;
};

MediaCue &dingCue() {
    static MediaCue cue(QUrl(QStringLiteral("qrc:/sounds/ding.wav")), 0.8f);
    return cue;
}

MediaCue &completedCue() {
    static MediaCue cue(QUrl(QStringLiteral("qrc:/sounds/completed.wav")), 0.8f);
    return cue;
}
}

namespace PlatformIntegration {
void initializeSounds() {
    dingCue().preload();
    completedCue().preload();
}

bool ensureAccessibilityPermissionPrompted() {
    return true;
}

void copyToClipboard(const QString &text) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text, QClipboard::Clipboard);
    if (clipboard->supportsSelection()) {
        clipboard->setText(text, QClipboard::Selection);
    }
}

void pasteText(const QString &text) {
    copyToClipboard(text);
}

void playStartSound() {
    dingCue().play();
}

void playStopSound() {
    dingCue().play();
}

void playSuccessSound() {
    completedCue().play();
}

void playErrorSound() {
    dingCue().play();
}
}
