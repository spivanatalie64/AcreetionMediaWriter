/*
 * AcreetionOS Media Writer
 * Copyright (C) 2026 Natalie Spiva <natalie@acreetionos.org>
 *
 * Cinnamon Desktop Theme — adapts to system dark/light mode automatically.
 *   Dark:     AcreetionOS brand palette (acreetionos.org)
 *   Light:    Clean, bright palette with Cinnamon-friendly contrast
 *
 * Based on Fedora Media Writer by the Fedora Project
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic  // for ApplicationWindow theme access

QtObject {
    // Reflects the active GTK/system palette (dark or light mode).
    // Declared as a QML object so the theme adapts live to system theme changes.
    property SystemPalette sysPalette: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    // Detect system dark/light mode from the GTK/application palette
    // Cinnamon uses GTK theming — Qt's gtk3 platform theme exposes this
    readonly property bool isDark: {
        var windowColor = sysPalette.window
        // If the window background is dark (low luminance), we're in dark mode
        var luminance = 0.299 * windowColor.r + 0.587 * windowColor.g + 0.114 * windowColor.b
        return luminance < 0.5
    }

    // ── AcreetionOS brand colours ──────────────────────────────────────
    readonly property color green:          "#2ecc71"
    readonly property color greenHover:     "#27ae60"
    readonly property color greenPressed:   "#1e8449"
    readonly property color greenDim:       Qt.rgba(0.18, 0.80, 0.44, 0.1)

    readonly property color storm:          "#61afef"
    readonly property color flasher:        "#f39c12"
    readonly property color purple:         "#9b59b6"
    readonly property color danger:         "#e74c3c"

    // ── Surfaces — adapt to dark or light mode ─────────────────────────
    readonly property color surface:         isDark ? "#121212" : "#f5f5f5"
    readonly property color surfaceAlt:      isDark ? "#1a1a1a" : "#ffffff"
    readonly property color surfaceCard:     isDark ? "#222222" : "#ffffff"
    readonly property color surfaceDark:     isDark ? "#0d0d0d" : "#e0e0e0"
    readonly property color surfaceHover:    isDark ? "#2a2a2a" : "#ebebeb"

    // Borders
    readonly property color border:          isDark ? "#333333" : "#cccccc"

    // Text
    readonly property color textPrimary:     isDark ? "#e5e5e5" : "#1a1a1a"
    readonly property color textSecondary:   isDark ? "#b2b2b2" : "#666666"
    readonly property color textOnAccent:    "#000000"

    // Link colour — matches system GTK link color on Cinnamon
    readonly property color linkColor:       isDark ? "#61afef" : "#2a7ab0"

    // Status
    readonly property color success:         "#2ecc71"
    readonly property color warning:         "#f39c12"
    readonly property color error:           "#e74c3c"

    // Accent — use the brand green, or in light mode a slightly darker shade
    readonly property color accent:          isDark ? green : "#27ae60"
    readonly property color accentHover:     isDark ? greenHover : "#219a52"
    readonly property color accentPressed:   isDark ? greenPressed : "#1a7a3e"

    // Button helper
    function buttonBackground(enabled, hovered, pressed) {
        if (!enabled) return surfaceCard;
        if (pressed) return accentPressed;
        if (hovered) return accentHover;
        return accent;
    }

    // Indicator (radio / checkbox)
    readonly property color indicatorBorder:    isDark ? "#555555" : "#999999"
    readonly property color indicatorChecked:   accent
}
