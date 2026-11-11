// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPixmap>
#include <QWidget>

// Square preview that accepts an image dropped on it or chosen with a click.
class IconDropZone : public QWidget {
    Q_OBJECT
public:
    explicit IconDropZone(int size, QWidget* parent = nullptr);

    void SetIcon(const QPixmap& icon);

signals:
    void FileDropped(const QString& path);
    void Clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QPixmap icon;
    bool dragging = false;
    bool hovered = false;
};
