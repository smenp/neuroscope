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

    explicit Palette(const QList<int>& clusterIds)
    {
        for (int id: clusterIds)
            colors.append(id, Qt::red);
        palette.resize(300, 400);
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
};

QTEST_MAIN(TestItemPalette)
#include "test_itempalette.moc"
