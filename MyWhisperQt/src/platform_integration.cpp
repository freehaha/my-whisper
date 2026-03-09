#include "platform_integration.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QThread>
#include <QTimer>

#if defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#endif

namespace {
QMimeData *cloneMimeData(const QMimeData *source) {
    if (!source) {
        return nullptr;
    }

    auto *copy = new QMimeData();
    for (const QString &format : source->formats()) {
        copy->setData(format, source->data(format));
    }
    return copy;
}

void restoreClipboardMode(QClipboard::Mode mode, QMimeData *mimeData) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!mimeData) {
        clipboard->clear(mode);
        return;
    }

    clipboard->setMimeData(mimeData, mode);
}

#if defined(Q_OS_LINUX)
bool x11Available() {
    return QGuiApplication::platformName().contains(QStringLiteral("xcb"), Qt::CaseInsensitive);
}

bool sendCtrlVOnX11() {
    if (!x11Available()) {
        return false;
    }

    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }

    const KeyCode ctrlKey = XKeysymToKeycode(display, XK_Control_L);
    const KeyCode vKey = XKeysymToKeycode(display, XK_v);
    if (ctrlKey == 0 || vKey == 0) {
        XCloseDisplay(display);
        return false;
    }

    XTestFakeKeyEvent(display, ctrlKey, True, CurrentTime);
    XTestFakeKeyEvent(display, vKey, True, CurrentTime);
    XTestFakeKeyEvent(display, vKey, False, CurrentTime);
    XTestFakeKeyEvent(display, ctrlKey, False, CurrentTime);
    XFlush(display);
    XCloseDisplay(display);
    return true;
}
#endif
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
    QClipboard *clipboard = QGuiApplication::clipboard();
    QMimeData *clipboardBackup = cloneMimeData(clipboard->mimeData(QClipboard::Clipboard));
    QMimeData *selectionBackup = clipboard->supportsSelection()
        ? cloneMimeData(clipboard->mimeData(QClipboard::Selection))
        : nullptr;

    copyToClipboard(text);
    QGuiApplication::processEvents();
    QThread::msleep(50);

#if defined(Q_OS_LINUX)
    const bool pasted = sendCtrlVOnX11();
#else
    const bool pasted = false;
#endif

    if (!pasted) {
        QApplication::beep();
    }

    QTimer::singleShot(500, qApp, [clipboardBackup, selectionBackup]() {
        restoreClipboardMode(QClipboard::Clipboard, clipboardBackup);
        if (QGuiApplication::clipboard()->supportsSelection()) {
            restoreClipboardMode(QClipboard::Selection, selectionBackup);
        } else {
            delete selectionBackup;
        }
    });
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
