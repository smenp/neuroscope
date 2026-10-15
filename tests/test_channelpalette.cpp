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
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <memory>

namespace
{

/** Default grouping, identical in both palettes: group id -> channel ids (group 0 is the trash). */
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

/** The two palettes of the main window, wired to each other the way the application wires them.
 * The channels of groups are numbered from 0 without gaps.*/
struct Palettes
{
    QWidget window;
    ChannelColors colors;
    QStringList labels;
    QMap<int, QList<int>> displayGroups;
    QMap<int, int> displayChannels;
    QMap<int, QList<int>> spikeGroups;
    QMap<int, int> spikeChannels;
    ChannelPalette* display;
    ChannelPalette* spike;

    explicit Palettes(const QMap<int, QList<int>>& groups = initialGroups())
        : displayGroups(groups),
          displayChannels(channelsToGroups(groups)),
          spikeGroups(groups),
          spikeChannels(channelsToGroups(groups))
    {
        for (int channel = 0; channel < displayChannels.size(); ++channel)
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
        // The application hands the selection of the active palette to the trace view, which hands it back to both
        // palettes, and then selects the same channels in the other palette.
        QObject::connect(display, &ChannelPalette::channelsSelected, &window,
                         [this](const QList<int>& ids)
                         {
                             spike->selectChannels(ids);
                             display->selectChannels(ids);
                             spike->selectChannels(ids);
                         });

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

/** Drags the selected channels of the palette by the item of @p byChannel and drops them on the item of @p ontoChannel. */
void dragOnto(ChannelPalette* palette, int byChannel, int ontoChannel)
{
    QListWidgetItem* handle = item(palette, byChannel);
    QListWidgetItem* onto = item(palette, ontoChannel);
    auto* source = static_cast<ChannelIconView*>(handle->listWidget());
    auto* target = static_cast<ChannelIconView*>(onto->listWidget());
    QWindow* window = palette->window()->windowHandle();
    const QPoint press = inWindow(source, source->visualItemRect(handle).center());
    const QPoint drop = inWindow(target, target->visualItemRect(onto).center());

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

int lastItemBottom(ChannelIconView* view)
{
    return view->visualItemRect(view->item(view->count() - 1)).bottom();
}

/** Height of a group box below the bottom of its last channel. */
int spaceBelowChannels(ChannelPalette* palette, int id)
{
    ChannelIconView* view = group(palette, id);
    QWidget* box = view->parentWidget();
    return box->height() - view->viewport()->mapTo(box, QPoint(0, lastItemBottom(view))).y();
}

/** Box of a group: its label and its channels. */
ChannelGroupView* box(ChannelPalette* palette, int id)
{
    return static_cast<ChannelGroupView*>(group(palette, id)->parentWidget());
}

/** Window position of a point of the box of a group. */
QPoint inWindow(ChannelPalette* palette, int id, const QPoint& boxPos)
{
    ChannelGroupView* groupBox = box(palette, id);
    return groupBox->mapTo(groupBox->window(), boxPos);
}

/** Drags a group by its label and drops it at the window position target() gives once the mouse is over it.
 * Calls during(drag) while the group is dragged. */
void dragGroup(ChannelPalette* palette, int id, const std::function<QPoint()>& target, const std::function<void(QDrag*)>& during = {})
{
    QLabel* label = box(palette, id)->label();
    QWindow* window = palette->window()->windowHandle();
    const QPoint press = label->mapTo(label->window(), label->rect().center());
    bool released = false;
    bool settled = false;
    const auto release = [&]()
    {
        released = true;
        if (during)
            during(label->findChild<QDrag*>());
        // The palette under the mouse scrolls to keep it away from its edges, which moves the target.
        QTest::mouseMove(window, target());
        const QPoint drop = target();
        QTest::mouseMove(window, drop);
        settled = target() == drop;
        QTest::mouseRelease(window, Qt::LeftButton, {}, drop);
    };
    // The label starts the drag as soon as it is pressed; QDrag::exec() runs its own event loop.
    QObject context;
    QTimer::singleShot(0, &context, release);
    QTest::mousePress(window, Qt::LeftButton, {}, press);
    QCoreApplication::processEvents();
    QVERIFY2(released, "the label did not start a drag");
    QVERIFY2(settled, "the palette kept scrolling under the mouse");
}

/** Window position halfway between the bottom of the box of group upper and the top of the box of group lower. */
QPoint between(ChannelPalette* palette, int upper, int lower)
{
    const QPoint bottom = inWindow(palette, upper, QPoint(box(palette, upper)->width() / 2, box(palette, upper)->height()));
    const QPoint top = inWindow(palette, lower, QPoint(box(palette, lower)->width() / 2, 0));
    return (bottom + top) / 2;
}

} // namespace

class TestChannelPalette : public QObject
{
    Q_OBJECT

  private:
    std::unique_ptr<Palettes> palettes;

    /** Shows the palettes with the given groups. */
    void showPalettes(const QMap<int, QList<int>>& groups)
    {
        palettes = std::make_unique<Palettes>(groups);
        palettes->window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&palettes->window));
    }

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
            QList<int> expected(palettes->labels.size());
            std::iota(expected.begin(), expected.end(), 0);
            QCOMPARE(all, expected);
        }
        QCOMPARE(shown(palettes->display, 0), shown(palettes->spike, 0));
    }

  private Q_SLOTS:
    void init()
    {
        QTest::failOnWarning(QRegularExpression("QObject::(dis)?connect"));
        showPalettes(initialGroups());
        verifyConsistent();
    }

    void cleanup()
    {
        palettes.reset();
    }

    // Moving selected channels deletes their items while they are selected.
    void moveChannelsToANewGroup()
    {
        select(palettes->display, { 1, 2, 3 });
        palettes->display->createGroup();
        verifyConsistent();
        QCOMPARE(shown(palettes->display, 3), (QList<int>{ 1, 2, 3 }));
        for (ChannelPalette* palette: { palettes->display, palettes->spike })
        {
            QList<int> selected = palette->selectedChannels();
            std::sort(selected.begin(), selected.end());
            QCOMPARE(selected, (QList<int>{ 1, 2, 3 }));
        }
    }

    void dragChannels_data()
    {
        QTest::addColumn<bool>("spikePalette");
        QTest::addColumn<QList<int>>("channels");
        QTest::addColumn<int>("onto");
        QTest::newRow("several channels to another group") << false << QList<int>{ 0, 2 } << 6;
        QTest::newRow("within a group") << false << QList<int>{ 0 } << 3;
        QTest::newRow("in the spike palette") << true << QList<int>{ 4 } << 1;
    }

    // Dragged channels move to the group of the channel they are dropped on and stay selected; the other palette is
    // unchanged.
    void dragChannels()
    {
        QFETCH(bool, spikePalette);
        QFETCH(QList<int>, channels);
        QFETCH(int, onto);
        ChannelPalette* palette = spikePalette ? palettes->spike : palettes->display;
        const QMap<int, int>& groupOf = spikePalette ? palettes->spikeChannels : palettes->displayChannels;
        const QMap<int, QList<int>>& otherGroups = spikePalette ? palettes->displayGroups : palettes->spikeGroups;
        const QMap<int, QList<int>> otherGroupsBefore = otherGroups;
        const int target = groupOf.value(onto);
        const QList<int> targetBefore = shown(palette, target);
        select(palette, channels);

        dragOnto(palette, channels.first(), onto);

        verifyConsistent();
        for (int channel: channels)
            QCOMPARE(groupOf.value(channel), target);
        QVERIFY(shown(palette, target) != targetBefore);
        QCOMPARE(otherGroups, otherGroupsBefore);
        QList<int> selected = palette->selectedChannels();
        std::sort(selected.begin(), selected.end());
        QCOMPARE(selected, channels);
    }

    void trashStaysInStep_data()
    {
        QTest::addColumn<QList<int>>("channels");
        QTest::addColumn<int>("onto");
        QTest::addColumn<QList<int>>("trash");
        // The trash holds 8 and 9.
        QTest::newRow("drag to the front") << QList<int>{ 2 } << 8 << QList<int>{ 2, 8, 9 };
        QTest::newRow("drag several between") << QList<int>{ 0, 1 } << 9 << QList<int>{ 8, 0, 1, 9 };
        QTest::newRow("drag out") << QList<int>{ 9 } << 0 << QList<int>{ 8 };
    }

    // The trash of both palettes holds the same channels in the same order.
    void trashStaysInStep()
    {
        QFETCH(QList<int>, channels);
        QFETCH(int, onto);
        QFETCH(QList<int>, trash);
        select(palettes->display, channels);

        dragOnto(palettes->display, channels.first(), onto);

        verifyConsistent();
        QCOMPARE(shown(palettes->display, 0), trash);
    }

    void groupsShrinkWhenChannelsLeave()
    {
        ChannelIconView* view = group(palettes->display, 1);
        const int perRow = view->viewport()->width() / view->gridSize().width();
        // Groups several rows high, so that moving channels out of one removes rows.
        const int perGroup = 4 * perRow;
        QMap<int, QList<int>> groups;
        for (int channel = 0; channel < 2 * perGroup + 2; ++channel)
            groups[channel < 2 * perGroup ? channel / perGroup + 1 : 0] << channel;
        showPalettes(groups);
        ChannelIconView* source = group(palettes->display, 1);
        const int sourceBottom = lastItemBottom(source);
        QCOMPARE(spaceBelowChannels(palettes->display, 1), spaceBelowChannels(palettes->display, 2));

        select(palettes->display, groups[1].mid(1));
        // Nothing guarantees that the palette is repainted after the move: rule out that a repaint adjusts the groups.
        palettes->window.hide();
        palettes->display->createGroup();
        QCoreApplication::processEvents();
        verifyConsistent();
        QVERIFY(lastItemBottom(source) < sourceBottom);
        QCOMPARE(spaceBelowChannels(palettes->display, 1), spaceBelowChannels(palettes->display, 2));
    }

    void groupsAreAdjustedOncePerMove()
    {
        palettes->window.hide();
        QSignalSpy adjustments(palettes->display, &ChannelPalette::paletteResized);
        const auto adjustmentsToMove = [&](const QList<int>& channels)
        {
            select(palettes->display, channels);
            adjustments.clear();
            palettes->display->createGroup();
            QCoreApplication::processEvents();
            return adjustments.count();
        };
        QCOMPARE(adjustmentsToMove({ 0, 1, 2 }), adjustmentsToMove({ 4 }));
        verifyConsistent();
    }

    void selectionSignalledOncePerMove_data()
    {
        using Move = std::function<void(ChannelPalette*)>;
        QTest::addColumn<Move>("move");
        QTest::addColumn<int>("displaySignals");
        QTest::addColumn<int>("spikeSignals");
        QTest::newRow("new group") << Move([](ChannelPalette* palette) { palette->createGroup(); }) << 1 << 0;
        QTest::newRow("drag to group") << Move([](ChannelPalette* palette) { dragOnto(palette, 0, 5); }) << 1 << 0;
        QTest::newRow("drag within group") << Move([](ChannelPalette* palette) { dragOnto(palette, 0, 3); }) << 1 << 0;
        QTest::newRow("drag to trash") << Move([](ChannelPalette* palette) { dragOnto(palette, 0, 9); }) << 1 << 1;
    }

    void selectionSignalledOncePerMove()
    {
        QFETCH(std::function<void(ChannelPalette*)>, move);
        QFETCH(int, displaySignals);
        QFETCH(int, spikeSignals);
        select(palettes->display, { 0, 1, 2 });
        QSignalSpy display(palettes->display, &ChannelPalette::channelsSelected);
        QSignalSpy spike(palettes->spike, &ChannelPalette::channelsSelected);
        move(palettes->display);
        verifyConsistent();
        QCOMPARE(display.count(), displaySignals);
        QCOMPARE(spike.count(), spikeSignals);
    }

    void selectionSignalledAfterDiscardingNothing()
    {
        select(palettes->display, {});
        palettes->display->discardChannels();
        QSignalSpy selections(palettes->display, &ChannelPalette::channelsSelected);
        item(palettes->display, 1)->setSelected(true);
        QCOMPARE(selections.count(), 1);
    }

    void dragGroupShowsItsLabel()
    {
        QLabel* label = box(palettes->display, 1)->label();
        QVERIFY(!label->size().isEmpty());
        QImage dragged;
        QPoint hotSpot;
        const QPoint press = label->mapTo(label->window(), label->rect().center());
        dragGroup(palettes->display, 1, [=]() { return press; },
                  [&](QDrag* drag)
                  {
                      QVERIFY(drag);
                      dragged = drag->pixmap().toImage();
                      hotSpot = drag->hotSpot();
                  });
        QCOMPARE(dragged, label->grab().toImage());
        QCOMPARE(hotSpot, label->rect().center());
    }

    void dropGroup_data()
    {
        QTest::addColumn<int>("source");
        QTest::addColumn<std::function<QPoint(ChannelPalette*)>>("drop");
        QTest::addColumn<bool>("scrolled");
        QTest::addColumn<QList<int>>("firstChannels");
        using Drop = std::function<QPoint(ChannelPalette*)>;
        const auto gap = [](int upper, int lower) { return Drop([=](ChannelPalette* palette) { return between(palette, upper, lower); }); };
        const auto inBox = [](int id, double height)
        { return Drop([=](ChannelPalette* palette) { return inWindow(palette, id, QPoint(box(palette, id)->width() / 2, box(palette, id)->height() * height)); }); };
        // Groups 1 to 4 hold the channels {0, 1}, {2, 3}, {4, 5} and {6, 7}; the trash holds {8, 9}.
        QTest::newRow("down into gap") << 1 << gap(2, 3) << false << QList<int>{ 2, 0, 4, 6 };
        QTest::newRow("down below last group") << 3 << gap(4, 0) << false << QList<int>{ 0, 2, 6, 4 };
        QTest::newRow("up into gap") << 4 << gap(1, 2) << false << QList<int>{ 0, 6, 2, 4 };
        QTest::newRow("up above first group") << 3 << inBox(1, -0.1) << false << QList<int>{ 4, 0, 2, 6 };
        QTest::newRow("onto itself") << 2 << inBox(2, 0.75) << false << QList<int>{ 0, 2, 4, 6 };
        QTest::newRow("scrolled, up into gap") << 4 << gap(2, 3) << true << QList<int>{ 0, 2, 6, 4 };
    }

    // A dropped group goes after the last group whose middle is above the drop, wherever the palette is scrolled.
    void dropGroup()
    {
        QFETCH(int, source);
        QFETCH(std::function<QPoint(ChannelPalette*)>, drop);
        QFETCH(bool, scrolled);
        QFETCH(QList<int>, firstChannels);
        showPalettes({ { 0, { 8, 9 } }, { 1, { 0, 1 } }, { 2, { 2, 3 } }, { 3, { 4, 5 } }, { 4, { 6, 7 } } });
        ChannelPalette* palette = palettes->display;
        QScrollBar* scrollBar = palette->verticalScrollBar();
        if (scrolled)
        {
            // One group shorter than the groups, so that the palette scrolls by a group.
            const int excess = palette->widget()->minimumSizeHint().height() - box(palette, 1)->height() - palette->viewport()->height();
            palettes->window.resize(palettes->window.width(), palettes->window.height() + excess);
            QTRY_VERIFY(scrollBar->maximum() > 0);
        }
        QPoint dropPos;
        int scrolledAtDrop = 0;
        dragGroup(
            palette, source,
            [&]()
            {
                dropPos = drop(palette);
                scrolledAtDrop = scrollBar->value();
                return dropPos;
            },
            [&](QDrag*)
            {
                // Scrolled once the drag has started, since pressing a label near the top of the palette scrolls it up.
                if (scrolled)
                    scrollBar->setValue(scrollBar->maximum());
            });
        QVERIFY2(palette->viewport()->rect().contains(palette->viewport()->mapFrom(&palettes->window, dropPos)), "the drop is outside the palette");
        QCOMPARE(scrolledAtDrop > 0, scrolled);
        verifyConsistent();
        QList<int> shownFirst;
        for (int id = 1; id <= 4; ++id)
            shownFirst << shown(palettes->display, id).value(0, -1);
        QCOMPARE(shownFirst, firstChannels);
        QCOMPARE(box(palettes->display, 1)->label()->text(), QString("1"));
    }
};

QTEST_MAIN(TestChannelPalette)
#include "test_channelpalette.moc"
