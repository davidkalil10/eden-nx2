// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/icon_drop_zone.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImageReader>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

namespace {

QPixmap RoundedPreview(const QPixmap& source, int size) {
    const qreal ratio = 2.0;
    QPixmap out(QSize(size, size) * ratio);
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), size / 8.0, size / 8.0);
    painter.setClipPath(clip);
    painter.drawPixmap(QRectF(0, 0, size, size), source, source.rect());
    return out;
}

QString DroppedImage(const QMimeData* mime) {
    if (mime == nullptr) {
        return {};
    }
    for (const auto& url : mime->urls()) {
        if (url.isLocalFile() && !QImageReader(url.toLocalFile()).format().isEmpty()) {
            return url.toLocalFile();
        }
    }
    return {};
}

} // namespace

IconDropZone::IconDropZone(int size, QWidget* parent) : QWidget(parent) {
    setFixedSize(size, size);
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Drop an image here or click to browse"));
}

void IconDropZone::SetIcon(const QPixmap& new_icon) {
    icon = new_icon;
    update();
}

void IconDropZone::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
    const bool active = dragging || hovered;
    if (!icon.isNull()) {
        painter.drawPixmap(0, 0, RoundedPreview(icon, width()));
        if (active) {
            painter.setBrush(QColor(0, 0, 0, 120));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(area, width() / 8.0, width() / 8.0);
            painter.setPen(Qt::white);
            painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, tr("Drop to replace"));
        }
        return;
    }
    const auto accent = palette().color(QPalette::Highlight);
    QPen pen(active ? accent : palette().color(QPalette::Mid), 2, Qt::DashLine);
    painter.setPen(pen);
    QColor fill = active ? accent : palette().color(QPalette::Mid);
    fill.setAlpha(active ? 40 : 20);
    painter.setBrush(fill);
    painter.drawRoundedRect(area, width() / 8.0, width() / 8.0);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(area.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                     dragging ? tr("Release to use this image")
                              : tr("Drop an icon here\nor click to browse"));
}

void IconDropZone::dragEnterEvent(QDragEnterEvent* event) {
    if (!DroppedImage(event->mimeData()).isEmpty()) {
        dragging = true;
        event->acceptProposedAction();
        update();
    }
}

void IconDropZone::dragLeaveEvent(QDragLeaveEvent*) {
    dragging = false;
    update();
}

void IconDropZone::dropEvent(QDropEvent* event) {
    dragging = false;
    update();
    if (const auto path = DroppedImage(event->mimeData()); !path.isEmpty()) {
        event->acceptProposedAction();
        emit FileDropped(path);
    }
}

void IconDropZone::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit Clicked();
    }
}

void IconDropZone::enterEvent(QEnterEvent*) {
    hovered = true;
    update();
}

void IconDropZone::leaveEvent(QEvent*) {
    hovered = false;
    update();
}
