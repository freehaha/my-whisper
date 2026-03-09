#include "platform_integration.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>

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
#if defined(Q_OS_LINUX)
    copyToClipboard(text);
#else
    copyToClipboard(text);
#endif
    QApplication::beep();
}

void playStartSound() {
    QApplication::beep();
}

void playStopSound() {
    QApplication::beep();
}

void playSuccessSound() {
    QApplication::beep();
}

void playErrorSound() {
    QApplication::beep();
}
}
