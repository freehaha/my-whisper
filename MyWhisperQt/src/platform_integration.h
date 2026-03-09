#pragma once

#include <QString>

namespace PlatformIntegration {
bool ensureAccessibilityPermissionPrompted();
void copyToClipboard(const QString &text);
void pasteText(const QString &text);
void playStartSound();
void playStopSound();
void playSuccessSound();
void playErrorSound();
}
