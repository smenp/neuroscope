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
#include "tracewidget.h"

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
};

} // namespace

Q_DECLARE_METATYPE(testutils::Spike)
Q_DECLARE_METATYPE(Kind)

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
        }
        QMetaObject::invokeMethod(app->findChild<TraceWidget*>(), "slotSetStartAndDuration", Q_ARG(long, 100), Q_ARG(long, 400));
        return app;
    }

    static void browseToNext(NeuroscopeApp* app, Kind kind)
    {
        QMetaObject::invokeMethod(app, kind == Kind::Spikes ? "slotShowNextCluster" : "slotShowNextEvent");
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
        for (Kind kind: { Kind::Spikes })
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
};

QTEST_MAIN(TestBrowsing)
#include "test_browsing.moc"
