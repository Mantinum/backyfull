#ifndef ICONUTILS_H
#define ICONUTILS_H

#include <QPixmap>
#include <QPainter>
#include <QColor>

inline QPixmap makeStatusDot(const QColor &color, int size = 12)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size - 1, size - 1);
    return pm;
}

#endif // ICONUTILS_H
