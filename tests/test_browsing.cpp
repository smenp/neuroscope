/***************************************************************************
                          test_browsing.cpp  -  description
                             -------------------
    purpose              : Tests for browsing spikes and events in the
                           trace view
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

#include "apptestutils.h"
#include "itemiconview.h"
#include "itempalette.h"
#include "neuroscopeview.h"
#include "tracewidget.h"

#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 4;
const double SAMPLING_RATE = 20000;
const std::int64_t NB_SAMPLES = 200000;

/** What is browsed: the spikes of cluster files or the events of event files. */
enum class Kind
{
    Spikes,
    Events,
};

/** What is done in the palette of the browsed file. */
enum class Step
{
    ClickGroup,     ///< click on the group label: shows all items, or hides them
    MarkGroup,      ///< Ctrl+Alt click on the group label: marks the shown items for browsing, or unmarks them
    MarkSecondItem, ///< Ctrl+Alt click on the second item, if shown: marks it for browsing, or unmarks it
    ClickFirstItem, ///< click on the first item: shows only it
    DeselectAll,    ///< Edit > Deselect All
};

} // namespace

Q_DECLARE_METATYPE(testutils::Spike)
Q_DECLARE_METATYPE(Kind)
Q_DECLARE_METATYPE(Step)

class TestBrowsing : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir dir;
    int nbRecordings = 0;

    /**
     * Opens a new recording with a file for each list of @p files and shows the window from 100 ms to 500 ms. The spikes
     * of cluster 2, or the events of a description of their own, are shown and browsed; those of cluster 3 are not.
     */
    std::unique_ptr<NeuroscopeApp> openWith(Kind kind, const QList<QList<Spike>>& files)
    {
        const QString baseName = QString("recording%1").arg(++nbRecordings);
        auto app = openRecording(writeRecording(QDir(dir.path()), baseName, NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES));
        for (int file = 0; file < files.size(); ++file)
        {
            if (kind == Kind::Spikes)
            {
                const QString id = QString::number(file + 1);
                loadFile(app.get(), "slotLoadClusterFiles", writeClusterFile(QDir(dir.path()), baseName, file + 1, files[file]));
                app->activeView()->shownClustersUpdate(id, { 2 });
                // Loaded clusters are not browsed until they are enabled for browsing.
                app->activeView()->updateNoneBrowsingClusterList(id, {});
            }
            else
            {
                const QString id = QString("ev%1").arg(file);
                QString events;
                for (const Spike& spike: files[file])
                {
                    events += QString("%1\tcluster%2\n").arg(spike.time * 1000 / SAMPLING_RATE).arg(spike.cluster);
                }
                const QString eventPath = dir.filePath(QString("%1.%2.evt").arg(baseName, id));
                writeTextFile(eventPath, events);
                loadFile(app.get(), "slotLoadEventFiles", eventPath);
                // The descriptions are numbered from 1 in alphabetical order: cluster2 before cluster3.
                app->activeView()->shownEventsUpdate(id, { 1 });
                app->activeView()->updateNoneBrowsingEventList(id, {});
            }
        }
        QMetaObject::invokeMethod(app->findChild<TraceWidget*>(), "slotSetStartAndDuration", Q_ARG(long, 100), Q_ARG(long, 400));
        return app;
    }

    static void browseToNext(NeuroscopeApp* app, Kind kind)
    {
        QMetaObject::invokeMethod(app, kind == Kind::Spikes ? "slotShowNextCluster" : "slotShowNextEvent");
    }

    /** Does @p steps with the mouse in the palette of the only file of @p app. */
    static void inPalette(NeuroscopeApp* app, Kind kind, const QList<Step>& steps)
    {
        ItemPalette* palette = nullptr;
        for (ItemPalette* candidate: app->findChildren<ItemPalette*>())
            if (candidate->objectName().contains(kind == Kind::Spikes ? "clusterPanel" : "eventPanel"))
                palette = candidate;
        // The application shows the items selected in the palette of its current tab only.
        qobject_cast<QTabWidget*>(palette->parentWidget()->parentWidget())->setCurrentWidget(palette);
        ItemIconView* items = palette->findChild<ItemIconView*>();
        GroupNameLabel* label = palette->findChild<GroupNameLabel*>();
        const Qt::KeyboardModifiers ctrlAlt = Qt::ControlModifier | Qt::AltModifier;
        const auto clickItem = [items](int row, Qt::KeyboardModifiers modifiers)
        { QTest::mouseClick(items->viewport(), Qt::LeftButton, modifiers, items->visualItemRect(items->item(row)).center()); };
        for (Step step: steps)
        {
            switch (step)
            {
            case Step::ClickGroup:
                QTest::mouseClick(label, Qt::LeftButton);
                break;
            case Step::MarkGroup:
                QTest::mouseClick(label, Qt::LeftButton, ctrlAlt);
                break;
            case Step::MarkSecondItem:
                clickItem(1, ctrlAlt);
                break;
            case Step::ClickFirstItem:
                clickItem(0, Qt::NoModifier);
                break;
            case Step::DeselectAll:
                QMetaObject::invokeMethod(app, "slotDeselectAll");
                break;
            }
        }
    }

  private Q_SLOTS:
    void initTestCase()
    {
        isolateSettings();
        QVERIFY(dir.isValid());
    }

    void browseToNextAcrossFiles_data()
    {
        QTest::addColumn<Kind>("kind");
        QTest::addColumn<QList<Spike>>("other");
        for (Kind kind: { Kind::Spikes, Kind::Events })
        {
            const char* name = kind == Kind::Spikes ? "spike" : "event";
            QTest::addRow("no later %s", name) << kind << QList<Spike>{ { 3600, 2 } };
            QTest::addRow("no later shown %s", name) << kind << QList<Spike>{ { 3600, 2 }, { 80000, 3 } };
        }
    }

    // Browsing goes to the earliest next spike or event of all files, also when the other file has none. Each file has a
    // spike or event in every window shown, so that no window read comes back empty. The trace view asks the files in
    // an order that varies between runs but not within one, so the other file is loaded first in one recording and
    // second in another.
    void browseToNextAcrossFiles()
    {
        QFETCH(Kind, kind);
        QFETCH(QList<Spike>, other);
        for (bool otherFirst: { true, false })
        {
            QList<QList<Spike>> files = { { { 3000, 2 }, { 5200, 2 } } };
            files.insert(otherFirst ? 0 : 1, other);
            auto app = openWith(kind, files);

            browseToNext(app.get(), kind);

            // The search starts at 25 % of the window, at 200 ms, and shows the next one, at 260 ms, at 25 % of the window.
            const long startTime = app->activeView()->getStartTime();
            QVERIFY2(startTime == 160, qPrintable(QString("other file loaded %1: start %2 ms").arg(otherFirst ? "first" : "second").arg(startTime)));
        }
    }

    void browseWhatThePaletteMarks_data()
    {
        QTest::addColumn<Kind>("kind");
        QTest::addColumn<QList<Step>>("steps");
        QTest::addColumn<long>("startTime");
        // The two label clicks share one toggle: after ClickGroup has shown all items, MarkGroup first unmarks them.
        QTest::newRow("event, group marked and unmarked")
            << Kind::Events << QList<Step>{ Step::ClickGroup, Step::MarkGroup, Step::MarkGroup, Step::MarkGroup } << 100L;
        QTest::newRow("spike, item marked before its group")
            << Kind::Spikes << QList<Step>{ Step::ClickGroup, Step::MarkGroup, Step::MarkSecondItem, Step::MarkGroup } << 160L;
        QTest::newRow("event, item shown after Deselect All")
            << Kind::Events << QList<Step>{ Step::ClickGroup, Step::MarkGroup, Step::MarkGroup, Step::DeselectAll, Step::ClickFirstItem }
            << 100L;
    }

    // Browsing goes to the next spike or event of the items marked in the palette only. The first, second and third items
    // are clusters 0, 1 and 2, or the events described as cluster0, cluster1 and cluster2.
    void browseWhatThePaletteMarks()
    {
        QFETCH(Kind, kind);
        QFETCH(QList<Step>, steps);
        QFETCH(long, startTime);
        auto app = openWith(kind, { { { 3000, 1 }, { 5200, 1 }, { 6400, 0 }, { 7000, 2 } } });

        inPalette(app.get(), kind, steps);
        browseToNext(app.get(), kind);

        QCOMPARE(app->activeView()->getStartTime(), startTime);
    }
};

QTEST_MAIN(TestBrowsing)
#include "test_browsing.moc"
