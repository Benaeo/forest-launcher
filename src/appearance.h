#pragma once

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

// Widget rendering only: leave colors, icons and desktop settings to Qt.
namespace Appearance {
inline QString installedStyle(const QString &name) {
    for (const auto &key : QStyleFactory::keys()) {
        if (key.compare(name, Qt::CaseInsensitive) == 0) return key;
    }
    return {};
}

inline QString displayName(const QString &name) {
    if (name.compare("windows", Qt::CaseInsensitive) == 0) return "Windows 9x";
    if (name.compare("fusion", Qt::CaseInsensitive) == 0) return "Fusion";
    if (name.compare("breeze", Qt::CaseInsensitive) == 0) return "Breeze";
    return name;
}

inline QString qtDefaultStyle() {
    // Capture the platform-selected style before Forest changes it.
    static const QString value = QApplication::style()->objectName();
    return value;
}

inline QString resolvedStyle(const QString &selection) {
    if (selection != "default") {
        const auto installed = installedStyle(selection);
        if (!installed.isEmpty()) return installed;
    }
    const auto breeze = installedStyle("breeze");
    if (!breeze.isEmpty()) return breeze;
    const auto fusion = installedStyle("fusion");
    return fusion.isEmpty() ? qtDefaultStyle() : fusion;
}

// Reject stale bootstrap responses after an independently saved theme change.
inline quint64 &revision() {
    static quint64 value = 0;
    return value;
}

inline void apply(const QString &selection) {
    const auto original = qtDefaultStyle();
    const auto target = resolvedStyle(selection);
    if (QApplication::style()->objectName().compare(target, Qt::CaseInsensitive) == 0) return;
    auto *style = QStyleFactory::create(target);
    // An advertised plugin can still fail to load; Fusion is built into Qt.
    if (!style) style = QStyleFactory::create("Fusion");
    if (!style) style = QStyleFactory::create(original);
    if (style) QApplication::setStyle(style); // QApplication owns the style.
}

}
