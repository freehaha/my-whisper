#include "platform_integration.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSoundEffect>
#include <QUrl>

namespace {
QSoundEffect *dingEffect() {
    static QSoundEffect *effect = []() {
        auto *sound = new QSoundEffect(qApp);
        sound->setSource(QUrl(QStringLiteral("qrc:/sounds/ding.wav")));
        sound->setLoopCount(1);
        sound->setVolume(0.7f);
        return sound;
    }();
    return effect;
}

QSoundEffect *completedEffect() {
    static QSoundEffect *effect = []() {
        auto *sound = new QSoundEffect(qApp);
        sound->setSource(QUrl(QStringLiteral("qrc:/sounds/completed.wav")));
        sound->setLoopCount(1);
        sound->setVolume(0.7f);
        return sound;
    }();
    return effect;
}

void playEffect(QSoundEffect *effect) {
    effect->stop();
    effect->play();
}
}

namespace PlatformIntegration {
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
    playEffect(dingEffect());
}

void playStopSound() {
    playEffect(dingEffect());
}

void playSuccessSound() {
    playEffect(completedEffect());
}

void playErrorSound() {
    playEffect(dingEffect());
}
}
