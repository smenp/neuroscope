/***************************************************************************
                          widgettestutils.h  -  description
                             -------------------
    purpose              : Helpers to drive widgets in the tests
    copyright            : (C) 2026 smenp
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef WIDGETTESTUTILS_H
#define WIDGETTESTUTILS_H

#include <QAbstractItemView>
#include <QtTest>

namespace widgettestutils
{

#if __has_include(<QtTest/qtestwheel.h>)
#define WIDGETTESTUTILS_WHEEL

/** Turns the mouse wheel one step down over the first item of the view, the way the window system does.
 * Returns whether the window received the event: QTest queues it for the event dispatcher of the platform,
 * and the minimal platform on Windows never dispatches that queue. */
inline bool scrollWheelDown(QAbstractItemView* view)
{
    struct WheelSpy : QObject
    {
        bool received = false;
        bool eventFilter(QObject*, QEvent* event) override
        {
            received |= event->type() == QEvent::Wheel;
            return false;
        }
    } spy;
    QWindow* window = view->window()->windowHandle();
    window->installEventFilter(&spy);
    const QPoint pos = view->visualRect(view->model()->index(0, 0)).center();
    QTest::wheelEvent(window, view->viewport()->mapTo(view->window(), pos), QPoint(0, -QWheelEvent::DefaultDeltasPerStep));
    window->removeEventFilter(&spy);
    return spy.received;
}
#endif

} // namespace widgettestutils

#endif
