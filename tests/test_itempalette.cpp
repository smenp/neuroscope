/***************************************************************************
                          test_itempalette.cpp  -  description
                             -------------------
    purpose              : Tests for the layout of the cluster palette
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

#include "clustercolors.h"
#include "itemiconview.h"
#include "itempalette.h"

#include <QScrollBar>
#include <QtTest>

#include <memory>

namespace
{

const QString GROUP = QStringLiteral("1");

/** A cluster palette showing one cluster file whose clusters have the given ids. */
struct Palette
{
    ClusterColors colors;
    ItemPalette palette{ ItemPalette::CLUSTER, Qt::black };

    explicit Palette(const QList<int>& clusterIds, QSize size = QSize(300, 400))
    {
        for (int id: clusterIds)
            colors.append(id, Qt::red);
        palette.resize(size);
        palette.show();
        palette.createItemList(&colors, GROUP, 0);
        QCoreApplication::processEvents();
    }

    ItemIconView* view() const
    {
        return palette.findChild<ItemIconView*>(GROUP);
    }
};

} // namespace

class TestItemPalette : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void init()
    {
        QTest::failOnWarning(QRegularExpression("QObject::(dis)?connect"));
    }

    void clusterIdsAreNotElided()
    {
        Palette p({ 2, 99, 100, 1249, 20000 });
        ItemIconView* view = p.view();
        QVERIFY(view);
        const QFontMetrics metrics(view->font());
        for (int row = 0; row < view->count(); ++row)
        {
            QListWidgetItem* item = view->item(row);
            const QString label = item->text();
            // The style draws the text inside the item rectangle, less this margin on each side.
            const int textMargin = view->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, view) + 1;
            const int textWidth = view->visualItemRect(item).width() - 2 * textMargin;
            QCOMPARE(metrics.elidedText(label, Qt::ElideRight, textWidth), label);
        }
    }

    void twoDigitPalettesKeepTheirGeometry()
    {
        Palette p({ 0, 1, 2, 10, 42, 99 });
        ItemIconView* view = p.view();
        // The cells keep the width of two font pixel sizes unless an item needs more.
        const QFontInfo fontInfo(QFont("Helvetica", 8));
        int widestItem = 0;
        for (int row = 0; row < view->count(); ++row)
            widestItem = qMax(widestItem, view->sizeHintForIndex(view->model()->index(row, 0)).width());
        QCOMPARE(view->gridSize(), QSize(qMax(fontInfo.pixelSize() * 2, widestItem), 15 * 2));
    }

    void longListsScroll()
    {
        QList<int> ids(1250);
        std::iota(ids.begin(), ids.end(), 0);
        Palette p(ids);
        ItemIconView* view = p.view();
        // Every item is laid out inside the view, and the palette scrolls to the last one.
        QVERIFY(view->visualItemRect(view->item(view->count() - 1)).bottom() < view->height());
        QTRY_VERIFY(p.palette.verticalScrollBar()->isVisible());
        QVERIFY(p.palette.verticalScrollBar()->maximum() + p.palette.viewport()->height() >= p.palette.widget()->height());
    }
};

QTEST_MAIN(TestItemPalette)
#include "test_itempalette.moc"
