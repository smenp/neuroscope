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

    // Dragged channels move to the group of the channel they are dropped on; the other palette is unchanged.
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
    }
};

QTEST_MAIN(TestChannelPalette)
#include "test_channelpalette.moc"
