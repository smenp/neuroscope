/***************************************************************************
                          test_channelpalette.cpp  -  description
                             -------------------
    purpose              : Tests for editing the channel groups in the
                           anatomical and spike channel palettes
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

#include "channelcolors.h"
#include "channeliconview.h"
#include "channelpalette.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <memory>

namespace
{

const int NB_CHANNELS = 10;

/** Initial grouping, identical in both palettes: group id -> channel ids (group 0 is the trash). */
QMap<int, QList<int>> initialGroups()
{
    return { { 0, { 8, 9 } }, { 1, { 0, 1, 2, 3 } }, { 2, { 4, 5, 6, 7 } } };
}

QMap<int, int> channelsToGroups(const QMap<int, QList<int>>& groups)
{
    QMap<int, int> result;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        for (int channel: it.value())
            result.insert(channel, it.key());
    return result;
}

/** The two palettes of the main window, wired to each other the way the application wires them. */
struct Palettes
{
    QWidget window;
    ChannelColors colors;
    QStringList labels;
    QMap<int, QList<int>> displayGroups = initialGroups();
    QMap<int, int> displayChannels = channelsToGroups(displayGroups);
    QMap<int, QList<int>> spikeGroups = initialGroups();
    QMap<int, int> spikeChannels = channelsToGroups(spikeGroups);
    ChannelPalette* display;
    ChannelPalette* spike;

    Palettes()
    {
        for (int channel = 0; channel < NB_CHANNELS; ++channel)
        {
            colors.append(channel, Qt::red, Qt::green, Qt::blue);
            labels << QString::number(channel);
        }
        display = new ChannelPalette(ChannelPalette::DISPLAY, Qt::black, true, &window, "displayPanel");
        spike = new ChannelPalette(ChannelPalette::SPIKE, Qt::black, true, &window, "spikePanel");
        auto* layout = new QHBoxLayout(&window);
        layout->addWidget(display);
        layout->addWidget(spike);
        window.resize(600, 500);

        for (auto [from, to]: { std::pair{ display, spike }, std::pair{ spike, display } })
        {
            QObject::connect(from, SIGNAL(channelsMovedToTrash(QList<int>, int, bool)), to, SLOT(discardChannels(QList<int>, int, bool)));
            QObject::connect(from, SIGNAL(channelsMovedAroundInTrash(QList<int>, int, bool)), to, SLOT(trashChannelsMovedAround(QList<int>, int, bool)));
            QObject::connect(from, SIGNAL(channelsRemovedFromTrash(QList<int>)), to, SLOT(removeChannelsFromTrash(QList<int>)));
        }
        // The application forwards channels discarded in the active palette to the other one.
        QObject::connect(display, &ChannelPalette::channelsDiscarded, spike, qOverload<const QList<int>&>(&ChannelPalette::discardChannels));

        display->createChannelLists(&colors, &displayGroups, &displayChannels, &labels);
        spike->createChannelLists(&colors, &spikeGroups, &spikeChannels, &labels);
        display->setEditMode(true);
        spike->setEditMode(true);
    }
};

ChannelIconView* group(ChannelPalette* palette, int id)
{
    for (auto* view: palette->findChildren<ChannelIconView*>())
        if (view->objectName() == QString::number(id))
            return view;
    return nullptr;
}

QListWidgetItem* item(ChannelPalette* palette, int channel)
{
    for (auto* view: palette->findChildren<ChannelIconView*>())
        if (const auto items = view->findItems(channel); !items.isEmpty())
            return items.first();
    return nullptr;
}

/** Channel ids shown in a group, in display order. */
QList<int> shown(ChannelPalette* palette, int id)
{
    QList<int> ids;
    if (auto* view = group(palette, id))
        for (int row = 0; row < view->count(); ++row)
            ids << static_cast<ChannelIconViewItem*>(view->item(row))->getID();
    return ids;
}

void select(ChannelPalette* palette, const QList<int>& channels)
{
    for (auto* view: palette->findChildren<ChannelIconView*>())
        view->clearSelection();
    for (int channel: channels)
        item(palette, channel)->setSelected(true);
}

QPoint inWindow(ChannelIconView* view, const QPoint& viewportPos)
{
    return view->viewport()->mapTo(view->window(), viewportPos);
}

/** Drags the selected channels of the palette by their first item and drops them at viewportPos of target. */
void drag(ChannelPalette* palette, int byChannel, ChannelIconView* target, const QPoint& viewportPos)
{
    QListWidgetItem* handle = item(palette, byChannel);
    auto* source = static_cast<ChannelIconView*>(handle->listWidget());
    QWindow* window = palette->window()->windowHandle();
    const QPoint press = inWindow(source, source->visualItemRect(handle).center());
    const QPoint drop = inWindow(target, viewportPos);

    // QDrag::exec() runs its own event loop; the drop happens inside it, never in the loop the drag starts from.
    const int outerLoopLevel = QThread::currentThread()->loopLevel();
    bool dropped = false;
    QTimer dropper;
    dropper.callOnTimeout(window,
                          [&]()
                          {
                              if (QThread::currentThread()->loopLevel() == outerLoopLevel)
                                  return;
                              dropper.stop();
                              QTest::mouseMove(window, drop);
                              QTest::mouseRelease(window, Qt::LeftButton, {}, drop);
                              dropped = true;
                          });
    dropper.start(0);

    QTest::mousePress(window, Qt::LeftButton, {}, press);
    // A view may only arm the drag on the first move past the start distance and start it on the next one.
    const QPoint step(QApplication::startDragDistance() + 1, 0);
    QTest::mouseMove(window, press + step);
    if (!dropped)
        QTest::mouseMove(window, press + 2 * step);
    QVERIFY2(dropped, "the view did not start a drag");
    QCoreApplication::processEvents();
}

void dragOnto(ChannelPalette* palette, int byChannel, int ontoChannel)
{
    QListWidgetItem* onto = item(palette, ontoChannel);
    auto* target = static_cast<ChannelIconView*>(onto->listWidget());
    drag(palette, byChannel, target, target->visualItemRect(onto).center());
}

} // namespace

class TestChannelPalette : public QObject
{
    Q_OBJECT

  private:
    std::unique_ptr<Palettes> palettes;

    /** Every channel is shown exactly once in each palette, with its id, and the group maps agree with the views. */
    void verifyConsistent()
    {
        for (ChannelPalette* palette: { palettes->display, palettes->spike })
        {
            const bool isDisplay = palette == palettes->display;
            const auto& groups = isDisplay ? palettes->displayGroups : palettes->spikeGroups;
            const auto& channels = isDisplay ? palettes->displayChannels : palettes->spikeChannels;
            QList<int> all;
            for (auto* view: palette->findChildren<ChannelIconView*>())
            {
                const int id = view->objectName().toInt();
                const QList<int> ids = shown(palette, id);
                if (!ids.isEmpty())
                    QCOMPARE(groups.value(id), ids);
                for (int channel: ids)
                    QCOMPARE(channels.value(channel, -100), id);
                all << ids;
            }
            std::sort(all.begin(), all.end());
            QList<int> expected(NB_CHANNELS);
            std::iota(expected.begin(), expected.end(), 0);
            QCOMPARE(all, expected);
        }
        QCOMPARE(shown(palettes->display, 0), shown(palettes->spike, 0));
    }

  private Q_SLOTS:
    void init()
    {
        QTest::failOnWarning(QRegularExpression("QObject::(dis)?connect"));
        palettes = std::make_unique<Palettes>();
        palettes->window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&palettes->window));
        verifyConsistent();
    }

    void cleanup()
    {
        palettes.reset();
    }

    void dragOneChannelToAnotherGroup()
    {
        select(palettes->display, { 1 });
        dragOnto(palettes->display, 1, 5);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 1), (QList<int>{ 0, 2, 3 }));
        QVERIFY(shown(palettes->display, 2).contains(1));
    }

    void dragSeveralChannelsToAnotherGroup()
    {
        select(palettes->display, { 0, 2 });
        dragOnto(palettes->display, 0, 6);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 1), (QList<int>{ 1, 3 }));
        QCOMPARE(shown(palettes->display, 2).size(), 6);
    }

    void reorderWithinGroup()
    {
        select(palettes->display, { 0 });
        dragOnto(palettes->display, 0, 3);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 1).size(), 4);
        QVERIFY(shown(palettes->display, 1) != (QList<int>{ 0, 1, 2, 3 }));
    }

    void dragWholeGroupToAnotherGroup()
    {
        select(palettes->display, { 0, 1, 2, 3 });
        dragOnto(palettes->display, 0, 4);
        QTest::qWait(200); // the emptied group is removed by a timer
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 1).size(), 8);
    }

    void dragToFrontOfTrash()
    {
        select(palettes->display, { 2 });
        dragOnto(palettes->display, 2, 8);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 0), (QList<int>{ 2, 8, 9 }));
    }

    void dragSeveralIntoTrash()
    {
        select(palettes->display, { 0, 1 });
        dragOnto(palettes->display, 0, 9);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 0), (QList<int>{ 8, 0, 1, 9 }));
    }

    void reorderWithinTrash()
    {
        select(palettes->display, { 9 });
        dragOnto(palettes->display, 9, 8);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 0), (QList<int>{ 9, 8 }));
    }

    void dragOutOfTrash()
    {
        select(palettes->display, { 9 });
        dragOnto(palettes->display, 9, 0);
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 0), (QList<int>{ 8 }));
        QVERIFY(shown(palettes->display, 1).contains(9));
        QVERIFY(!shown(palettes->spike, 0).contains(9));
    }

    void dragInSpikePalette()
    {
        select(palettes->spike, { 4 });
        dragOnto(palettes->spike, 4, 1);
        verifyConsistent();
        QVERIFY(shown(palettes->spike, 1).contains(4));
        QCOMPARE(shown(palettes->display, 2), (QList<int>{ 4, 5, 6, 7 }));
    }

    void discardSelection()
    {
        select(palettes->display, { 3, 7 });
        palettes->display->discardChannels();
        verifyConsistent();
        QList<int> trash = shown(palettes->display, 0);
        std::sort(trash.begin(), trash.end());
        QCOMPARE(trash, (QList<int>{ 3, 7, 8, 9 }));
    }

    void moveSelectionToNewGroup()
    {
        select(palettes->display, { 1, 6 });
        palettes->display->createGroup();
        verifyConsistent();
        QList<int> moved = shown(palettes->display, 3);
        std::sort(moved.begin(), moved.end());
        QCOMPARE(moved, (QList<int>{ 1, 6 }));
    }

    void shiftClickSelectsRange()
    {
        ChannelIconView* view = group(palettes->display, 1);
        QWindow* window = palettes->window.windowHandle();
        select(palettes->display, {});
        const auto at = [&](int channel)
        { return inWindow(view, view->visualItemRect(item(palettes->display, channel)).center()); };
        QTest::mouseClick(window, Qt::LeftButton, {}, at(0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, at(2));
        QList<int> selected = palettes->display->selectedChannels();
        std::sort(selected.begin(), selected.end());
        QCOMPARE(selected, (QList<int>{ 0, 1, 2 }));
        verifyConsistent();
    }
};

QTEST_MAIN(TestChannelPalette)
#include "test_channelpalette.moc"
