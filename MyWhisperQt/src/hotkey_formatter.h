#pragma once

#include "types.h"

#include <QKeyEvent>
#include <QString>
#include <optional>

namespace HotkeyFormatter {
std::optional<HotkeyBinding> bindingFromKeyEvent(QKeyEvent *event);
quint32 carbonModifiers(Qt::KeyboardModifiers modifiers);
bool isModifierKey(quint32 keyCode);
QString keyName(quint32 keyCode);
QString displayString(const HotkeyBinding &binding);
}
