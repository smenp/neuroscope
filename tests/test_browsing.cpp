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
#include "neuroscopeview.h"
#include "traceview.h"
#include "tracewidget.h"

#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 4;
const double SAMPLING_RATE = 20000;
const std::int64_t NB_SAMPLES = 200000;

struct Spike
{
    std::int64_t time; // in samples
    int cluster;
};

} // namespace

Q_DECLARE_METATYPE(Spike)

namespace
{

/** Writes @p baseName.clu.@p id and @p baseName.res.@p id into @p dir and returns the path of the .clu file. */
QString writeClusterFile(const QDir& dir, const QString& baseName, int id, const QList<Spike>& spikes)
{
    QString clu = "3\n";
    QString res;
    for (const Spike& spike: spikes)
    {
        clu += QString::number(spike.cluster) + "\n";
        res += QString::number(spike.time) + "\n";
    }
    writeTextFile(dir.filePath(QString("%1.res.%2").arg(baseName).arg(id)), res);
    const QString cluPath = dir.filePath(QString("%1.clu.%2").arg(baseName).arg(id));
    writeTextFile(cluPath, clu);
    return cluPath;
}

} // namespace

class TestBrowsing : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir dir;

    /** Opens a recording with the given cluster files loaded and cluster 2 of each shown and browsed. */
    std::unique_ptr<NeuroscopeApp> openWithClusters(const QString& baseName, const QList<QList<Spike>>& files)
    {
        const QDir recordingDir(dir.path());
        auto app = openRecording(writeRecording(recordingDir, baseName, NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES));
        for (int id = 1; id <= files.size(); ++id)
        {
            loadFile(app.get(), "slotLoadClusterFiles", writeClusterFile(recordingDir, baseName, id, files[id - 1]));
            app->activeView()->shownClustersUpdate(QString::number(id), { 2 });
            // Loaded clusters are not browsed until they are enabled for browsing.
            app->activeView()->updateNoneBrowsingClusterList(QString::number(id), {});
        }
        return app;
    }

    /** Opens a recording with the given event files loaded, their events (all "stim") shown and browsed. */
    std::unique_ptr<NeuroscopeApp> openWithEvents(const QString& baseName, const QList<QList<double>>& files)
    {
        const QDir recordingDir(dir.path());
        auto app = openRecording(writeRecording(recordingDir, baseName, NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES));
        for (int file = 0; file < files.size(); ++file)
        {
            const QString id = QString("ev%1").arg(file);
            QString content;
            for (double time: files[file])
                content += QString::number(time) + "\tstim\n";
            const QString eventPath = recordingDir.filePath(QString("%1.%2.evt").arg(baseName, id));
            writeTextFile(eventPath, content);
            loadFile(app.get(), "slotLoadEventFiles", eventPath);
            if (files[file].isEmpty())
                continue;
            // The only description has the id 1.
            app->activeView()->shownEventsUpdate(id, { 1 });
            app->activeView()->updateNoneBrowsingEventList(id, {});
        }
        return app;
    }

    static void showWindow(NeuroscopeApp* app, long start, long duration)
    {
        QMetaObject::invokeMethod(app->findChild<TraceWidget*>(), "slotSetStartAndDuration", Q_ARG(long, start), Q_ARG(long, duration));
    }

  private Q_SLOTS:
    void initTestCase()
    {
        isolateSettings();
        QVERIFY(dir.isValid());
    }

    // Next Spike past the last spike of the only cluster file leaves the shown window and its spikes as they are.
    void nextSpikeAfterTheLastSpike()
    {
        auto app = openWithClusters("last", { { { 3000, 2 }, { 3200, 2 } } });
        showWindow(app.get(), 100, 400);
        TraceView* view = app->findChild<TraceView*>();
        const QImage before = view->grab().toImage();

        QMetaObject::invokeMethod(app.get(), "slotShowNextCluster");

        QCOMPARE(app->activeView()->getStartTime(), 100L);
        QCOMPARE(view->grab().toImage(), before);
    }

    // Next Spike goes to the earliest next spike of all cluster files, also when one of them has none. Each file has a
    // spike in every window shown, so that no window read comes back empty. The trace view asks the files in the
    // iteration order of a hash, which varies between runs, so each case is tried with the file without a next spike
    // loaded first and second.
    void nextSpikeAcrossClusterFiles_data()
    {
        QTest::addColumn<QList<Spike>>("other");
        QTest::addColumn<bool>("otherFirst");

        QTest::newRow("single file") << QList<Spike>{} << false;
        for (bool otherFirst: { true, false })
        {
            const char* order = otherFirst ? "loaded first" : "loaded second";
            QTest::addRow("no later spike, %s", order) << QList<Spike>{ { 3600, 2 } } << otherFirst;
            QTest::addRow("no later spike of a shown cluster, %s", order) << QList<Spike>{ { 3600, 2 }, { 80000, 3 } } << otherFirst;
        }
    }

    void nextSpikeAcrossClusterFiles()
    {
        QFETCH(QList<Spike>, other);
        QFETCH(bool, otherFirst);
        QList<QList<Spike>> files = { { { 3000, 2 }, { 5200, 2 } } };
        if (!other.isEmpty())
            files.insert(otherFirst ? 0 : 1, other);
        auto app = openWithClusters(QString("across-%1").arg(QTest::currentDataTag()).replace(QRegularExpression("[ ,]+"), "-"), files);
        showWindow(app.get(), 100, 400);

        QMetaObject::invokeMethod(app.get(), "slotShowNextCluster");

        // Next Spike looks after 25 % of the window, from 200 ms, and shows the spike at 260 ms at 25 % of the window.
        QCOMPARE(app->activeView()->getStartTime(), 160L);
    }

    // Next Event past the last event of the only event file leaves the shown window and its events as they are.
    void nextEventAfterTheLastEvent()
    {
        auto app = openWithEvents("lastevent", { { 150, 160 } });
        showWindow(app.get(), 100, 400);
        TraceView* view = app->findChild<TraceView*>();
        const QImage before = view->grab().toImage();

        QMetaObject::invokeMethod(app.get(), "slotShowNextEvent");

        QCOMPARE(app->activeView()->getStartTime(), 100L);
        QCOMPARE(view->grab().toImage(), before);
    }

    // Next Event goes to the earliest next event of all event files, also when one of them has none. Each file has an
    // event in every window shown, so that no window read comes back empty. The trace view asks the files in the
    // iteration order of a hash, which varies between runs, so the file without a next event is loaded first and second.
    void nextEventAcrossEventFiles_data()
    {
        QTest::addColumn<bool>("otherFirst");

        QTest::newRow("loaded first") << true;
        QTest::newRow("loaded second") << false;
    }

    void nextEventAcrossEventFiles()
    {
        QFETCH(bool, otherFirst);
        QList<QList<double>> files = { { 150, 260 } };
        files.insert(otherFirst ? 0 : 1, { 180 });
        auto app = openWithEvents(QString("acrossevents-%1").arg(QTest::currentDataTag()).replace(' ', '-'), files);
        showWindow(app.get(), 100, 400);

        QMetaObject::invokeMethod(app.get(), "slotShowNextEvent");

        // Next Event looks after 25 % of the window, from 200 ms, and shows the event at 260 ms at 25 % of the window.
        QCOMPARE(app->activeView()->getStartTime(), 160L);
    }
};

QTEST_MAIN(TestBrowsing)
#include "test_browsing.moc"
