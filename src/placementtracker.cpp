/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "placementtracker.h"
#include "core/output.h"
#include "window.h"
#include "workspace.h"

namespace KWin
{

PlacementTracker::PlacementTracker(Workspace *workspace)
    : m_workspace(workspace)
{
}

PlacementTracker::WindowData PlacementTracker::dataForWindow(Window *window) const
{
    return WindowData{
        .outputUuid = window->moveResizeOutput()->uuid(),
        .geometry = window->moveResizeGeometry(),
        .maximize = window->requestedMaximizeMode(),
        .quickTile = window->requestedQuickTileMode(),
        .geometryRestore = window->geometryRestore(),
        .fullscreen = window->isRequestedFullScreen(),
        .fullscreenGeometryRestore = window->fullscreenGeometryRestore(),
    };
}

void PlacementTracker::add(Window *window)
{
    if (window->isUnmanaged() || window->isAppletPopup() || window->isSpecialWindow()) {
        return;
    }
    connect(window, &Window::frameGeometryChanged, this, [this, window]() {
        saveGeometry(window);
    });
    connect(window, &Window::maximizedChanged, this, [this, window]() {
        saveMaximize(window);
    });
    connect(window, &Window::quickTileModeChanged, this, [this, window]() {
        saveQuickTile(window);
    });
    connect(window, &Window::fullScreenChanged, this, [this, window]() {
        saveFullscreen(window);
    });
    connect(window, &Window::maximizeGeometryRestoreChanged, this, [this, window]() {
        saveMaximizeGeometryRestore(window);
    });
    connect(window, &Window::fullscreenGeometryRestoreChanged, this, [this, window]() {
        saveFullscreenGeometryRestore(window);
    });
    connect(window, &Window::interactiveMoveResizeFinished, this, [this, window]() {
        if (m_inhibitCount == 0 && !m_workspace->inRearrange()) {
            invalidateOtherKeys(window);
            saveGeometry(window);
        }
    });
    WindowData data = dataForWindow(window);
    m_data[m_currentKey][window] = data;
    m_savedWindows.push_back(window);
}

void PlacementTracker::remove(Window *window)
{
    if (m_savedWindows.contains(window)) {
        disconnect(window, nullptr, this, nullptr);
        for (auto &dataMap : m_data) {
            dataMap.remove(window);
        }
        m_savedWindows.removeOne(window);
    }
}

void PlacementTracker::restore(const QString &key)
{
    if (key == m_currentKey) {
        return;
    }
    auto &dataMap = m_data[key];
    const auto outputs = m_workspace->outputs();

    inhibit();
    for (const auto window : std::as_const(m_savedWindows)) {
        const auto it = dataMap.find(window);
        if (it != dataMap.end()) {
            const WindowData &newData = it.value();

            window->setQuickTileMode(newData.quickTile, newData.geometry.center());
            window->setMaximize(newData.maximize & MaximizeMode::MaximizeVertical, newData.maximize & MaximizeMode::MaximizeHorizontal);
            window->setFullScreen(newData.fullscreen);
            window->moveResize(newData.geometry);
            window->setGeometryRestore(newData.geometryRestore);
            window->setFullscreenGeometryRestore(newData.fullscreenGeometryRestore);
            m_lastRestoreData[window] = dataForWindow(window);
        }
        // ensure data in current map is always up to date
        dataMap[window] = dataForWindow(window);
    }
    uninhibit();
    m_currentKey = key;
}

void PlacementTracker::init(const QString &key)
{
    m_currentKey = key;
}

void PlacementTracker::updateCurrentLayout()
{
    if (m_inhibitCount > 0 || m_currentKey.isEmpty()) {
        return;
    }
    auto &dataMap = m_data[m_currentKey];
    for (Window *window : std::as_const(m_savedWindows)) {
        dataMap[window] = dataForWindow(window);
    }
}

void PlacementTracker::invalidateOtherKeys(Window *window)
{
    for (auto it = m_data.begin(); it != m_data.end(); ++it) {
        if (it.key() != m_currentKey) {
            it.value().remove(window);
        }
    }
}

void PlacementTracker::saveGeometry(Window *window)
{
    if (m_inhibitCount == 0) {
        auto &data = m_data[m_currentKey][window];
        const RectF newGeometry = window->moveResizeGeometry();
        const QString newOutputUuid = window->moveResizeOutput()->uuid();
        if (!m_workspace->inRearrange() && (data.geometry.topLeft() != newGeometry.topLeft() || data.outputUuid != newOutputUuid)) {
            invalidateOtherKeys(window);
        }
        data.geometry = newGeometry;
        data.outputUuid = newOutputUuid;
    }
}

void PlacementTracker::saveMaximize(Window *window)
{
    if (m_inhibitCount == 0) {
        auto &data = m_data[m_currentKey][window];
        if (!m_workspace->inRearrange() && data.maximize != window->maximizeMode()) {
            invalidateOtherKeys(window);
        }
        data.maximize = window->maximizeMode();
    }
}

void PlacementTracker::saveQuickTile(Window *window)
{
    if (m_inhibitCount == 0) {
        auto &data = m_data[m_currentKey][window];
        if (!m_workspace->inRearrange() && data.quickTile != window->quickTileMode()) {
            invalidateOtherKeys(window);
        }
        data.quickTile = window->quickTileMode();
    }
}

void PlacementTracker::saveFullscreen(Window *window)
{
    if (m_inhibitCount == 0) {
        auto &data = m_data[m_currentKey][window];
        if (!m_workspace->inRearrange() && data.fullscreen != window->isFullScreen()) {
            invalidateOtherKeys(window);
        }
        data.fullscreen = window->isFullScreen();
    }
}

void PlacementTracker::saveMaximizeGeometryRestore(Window *window)
{
    if (m_inhibitCount == 0) {
        auto &data = m_data[m_currentKey][window];
        data.geometryRestore = window->geometryRestore();
    }
}

void PlacementTracker::saveFullscreenGeometryRestore(Window *window)
{
    if (m_inhibitCount == 0) {
        auto &data = m_data[m_currentKey][window];
        data.fullscreenGeometryRestore = window->fullscreenGeometryRestore();
    }
}

void PlacementTracker::inhibit()
{
    m_inhibitCount++;
}

void PlacementTracker::uninhibit()
{
    Q_ASSERT(m_inhibitCount > 0);
    m_inhibitCount--;
}

}

#include "moc_placementtracker.cpp"
