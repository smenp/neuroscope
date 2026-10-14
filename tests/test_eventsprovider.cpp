/***************************************************************************
                          test_eventsprovider.cpp  -  description
                             -------------------
    purpose              : Tests for reading and writing .evt files
    copyright            : (C) 2026 by Joscha Schmiedt
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "eventsprovider.h"
#include "testutils.h"

#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace testutils;

namespace
{

struct Event
{
    double time; // in ms
    QString description;
};

QString eventFileContent(const QList<Event>& events)
{
    QString content;
    for (const Event& event : events)
        content += QString::number(event.time, 'g', 12) + "\t" + event.description + "\n";
    return content;
}

struct Result
{
    Matrix times;
    Matrix ids;
    QString name;
    std::int64_t startingTime = -1;
};

Result request(EventsProvider& provider, std::int64_t startTime, std::int64_t endTime)
{
    Result result;
    int emitted = 0;
    auto connection = QObject::connect(&provider, &EventsProvider::dataReady,
                                       [&](Array<dataType>& times, Array<int>& ids, QObject*, QString name)
                                       {
                                           result.times = toMatrix(times);
                                           result.ids = toMatrix(ids);
                                           result.name = name;
                                           ++emitted;
                                       });
    provider.requestData(static_cast<long>(startTime), static_cast<long>(endTime), nullptr);
    QObject::disconnect(connection);
    if (emitted != 1)
        qFatal("dataReady emitted %d times", emitted);
    return result;
}

Result requestNext(EventsProvider& provider, std::int64_t startTime, std::int64_t timeFrame, const QList<int>& selectedIds)
{
    Result result;
    int emitted = 0;
    auto connection = QObject::connect(&provider, &EventsProvider::nextEventDataReady,
                                       [&](Array<dataType>& times, Array<int>& ids, QObject*, QString name, long startingTime)
                                       {
                                           result = {toMatrix(times), toMatrix(ids), name, startingTime};
                                           ++emitted;
                                       });
    provider.requestNextEventData(static_cast<long>(startTime), static_cast<long>(timeFrame), selectedIds, nullptr);
    QObject::disconnect(connection);
    if (emitted != 1)
        qFatal("nextEventDataReady emitted %d times", emitted);
    return result;
}

Result requestPrevious(EventsProvider& provider, std::int64_t startTime, std::int64_t timeFrame,
                       const QList<int>& selectedIds)
{
    Result result;
    int emitted = 0;
    auto connection = QObject::connect(&provider, &EventsProvider::previousEventDataReady,
                                       [&](Array<dataType>& times, Array<int>& ids, QObject*, QString name, long startingTime)
                                       {
                                           result = {toMatrix(times), toMatrix(ids), name, startingTime};
                                           ++emitted;
                                       });
    provider.requestPreviousEventData(static_cast<long>(startTime), static_cast<long>(timeFrame), selectedIds, nullptr);
    QObject::disconnect(connection);
    if (emitted != 1)
        qFatal("previousEventDataReady emitted %d times", emitted);
    return result;
}

QString describe(const Result& result)
{
    QStringList events;
    for (std::int64_t c = 1; c <= result.times.cols; ++c)
        events << QString("(%1, %2)").arg(result.times(1, c)).arg(result.ids(1, c));
    return events.join(' ');
}

/**
 * The events of a window computed directly from the event list: an event is on the sample nearest to it, counted from
 * the start of the window, and is in the window if the traces of the window have that sample.
 */
QString expectedWindow(const QList<Event>& events, const QMap<QString, int>& ids, std::int64_t startTime,
                       std::int64_t endTime,
                       double samplingRate)
{
    const double samplesPerMs = samplingRate / 1000.0;
    const std::int64_t lastSample = static_cast<std::int64_t>(endTime * samplesPerMs) - static_cast<std::int64_t>(startTime * samplesPerMs);
    QStringList result;
    for (const Event& event : events)
    {
        const std::int64_t sample = static_cast<std::int64_t>(std::floor(0.5 + (event.time - startTime) * samplesPerMs));
        if (sample < 0 || sample > lastSample)
        {
            continue;
        }
        result << QString("(%1, %2)").arg(sample).arg(ids[event.description]);
    }
    return result.join(' ');
}

/** The content of @p provider saved to @p path. */
QString saved(EventsProvider& provider, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !provider.save(&file))
        qFatal("Cannot save %s", qPrintable(path));
    file.close();
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(path));
    return QString::fromUtf8(file.readAll());
}

} // namespace

Q_DECLARE_METATYPE(Event)

class TestEventsProvider : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir dir;
    QList<Event> manyEvents;

    // Event descriptions are numbered in case-insensitive alphabetical order, starting at 1.
    const QMap<QString, int> ids = {{"lick", 1}, {"reward", 2}, {"Stim off", 3}, {"stim on", 4}};
    const QList<Event> fewEvents = {{100.0, "lick"}, {200.0, "reward"}, {500.0, "lick"}, {1500.0, "reward"}, {3000.0, "lick"}};

    QString path(const QString& name) const { return dir.filePath(name); }

  private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());

        // Events are at least 1 ms apart, at fractions of a millisecond.
        QRandomGenerator random(42);
        const QStringList descriptions = ids.keys();
        double time = 10.25;
        for (int i = 0; i < 5000; ++i)
        {
            // The time as the file stores it.
            manyEvents.append({QString::number(time, 'g', 12).toDouble(), descriptions[random.bounded(descriptions.size())]});
            time += 1.0 + random.bounded(400000) / 1000.0;
        }
        writeTextFile(path("many.abc.evt"), eventFileContent(manyEvents));
        writeTextFile(path("few.xyz.evt"), eventFileContent(fewEvents));
        writeTextFile(path("empty.emp.evt"), "");
    }

    void name_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("name");

        QTest::newRow("X.id.evt") << "session.r01.evt" << "r01";
        QTest::newRow("X.evt.id") << "session.evt.r01" << "r01";
    }

    void name()
    {
        QFETCH(QString, file);
        QFETCH(QString, name);
        EventsProvider provider(path(file), 20000.0);
        QCOMPARE(provider.getName(), name);
    }

    void load()
    {
        EventsProvider provider(path("many.abc.evt"), 20000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        QCOMPARE(provider.getNbEvents(), 5000);

        QMap<EventDescription, int> expectedIds;
        QMap<int, EventDescription> expectedDescriptions;
        for (auto it = ids.begin(); it != ids.end(); ++it)
        {
            expectedIds.insert(EventDescription(it.key()), it.value());
            expectedDescriptions.insert(it.value(), EventDescription(it.key()));
        }
        QCOMPARE(provider.eventDescriptionIdMap(), expectedIds);
        QCOMPARE(provider.eventIdDescriptionMap(), expectedDescriptions);
        QVERIFY(!provider.isModified());
    }

    void descriptionsWithSpaces()
    {
        writeTextFile(path("spaces.spc.evt"), "12.5 \t two  words here\n");
        EventsProvider provider(path("spaces.spc.evt"), 20000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        QCOMPARE(provider.eventIdDescriptionMap().value(1), EventDescription("two  words here"));
    }

    void emptyFile()
    {
        EventsProvider provider(path("empty.emp.evt"), 20000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        QCOMPARE(provider.getNbEvents(), 0);
        QVERIFY(request(provider, 0, 1000).times.isEmpty());
    }

    void readWindow_data()
    {
        QTest::addColumn<std::int64_t>("startTime");
        QTest::addColumn<std::int64_t>("endTime");

        QTest::newRow("start of file") << 0_i64 << 1000_i64;
        QTest::newRow("middle") << 300000_i64 << 301000_i64;
        QTest::newRow("end of file") << 990000_i64 << 2000000_i64;
        QTest::newRow("past end of file") << 2000000_i64 << 2001000_i64;
    }

    void readWindow()
    {
        QFETCH(std::int64_t, startTime);
        QFETCH(std::int64_t, endTime);

        EventsProvider provider(path("many.abc.evt"), 20000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        const Result result = request(provider, startTime, endTime);
        QCOMPARE(result.name, QString("abc"));
        QCOMPARE(describe(result), expectedWindow(manyEvents, ids, startTime, endTime, 20000.0));
    }

    void browseWindows_data()
    {
        QTest::addColumn<double>("samplingRate");

        QTest::newRow(".dat sampling rate") << 20000.0;
        QTest::newRow(".eeg sampling rate") << 1250.0;
    }

    void browseWindows()
    {
        // See TestClustersProvider::browseWindows.
        QFETCH(double, samplingRate);

        EventsProvider provider(path("many.abc.evt"), samplingRate);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));

        const std::int64_t fileMaxTime = static_cast<std::int64_t>(std::floor(0.5 + manyEvents.last().time));
        QRandomGenerator random(7);
        std::int64_t startTime = 0;
        std::int64_t duration = 1000;
        for (int i = 0; i < 500; ++i)
        {
            switch (random.bounded(4))
            {
            case 0:
                startTime += duration;
                break;
            case 1:
                startTime = qMax(0_i64, startTime - duration);
                break;
            case 2:
                startTime = random.bounded(static_cast<int>(fileMaxTime + 500));
                break;
            case 3:
                duration = 1 + random.bounded(5000);
                break;
            }
            const std::int64_t endTime = startTime + duration;
            const QString actual = describe(request(provider, startTime, endTime));
            const QString expected = expectedWindow(manyEvents, ids, startTime, endTime, samplingRate);
            if (actual != expected)
                QFAIL(qPrintable(QString("request %1: window %2-%3 ms\nactual:   %4\nexpected: %5")
                                     .arg(i).arg(startTime).arg(endTime).arg(actual, expected)));
            if (startTime > fileMaxTime)
                startTime = 0;
        }
    }

    // A window that ends before the first event reads none; reading on from its end starts at the first event.
    void readOnFromAWindowBeforeTheFirstEvent()
    {
        EventsProvider provider(path("few.xyz.evt"), 1000.0, 25);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        const int lick = provider.eventDescriptionIdMap().value(EventDescription("lick"));
        QVERIFY(request(provider, 0, 50).times.isEmpty());
        QCOMPARE(describe(request(provider, 50, 150)), QString("(50, %1)").arg(lick));
    }

    void previousEventFromAWindowBeforeTheFirstEvent()
    {
        EventsProvider provider(path("few.xyz.evt"), 1000.0, 25);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        const int lick = provider.eventDescriptionIdMap().value(EventDescription("lick"));
        QVERIFY(request(provider, 10, 60).times.isEmpty());

        // The browse point at 60 ms is the end of the last window and precedes every event.
        const Result previous = requestPrevious(provider, 10, 200, {lick});
        QVERIFY(previous.times.isEmpty());
        QCOMPARE(previous.startingTime, 10_i64);
    }

    void nextAndPreviousEvent()
    {
        // 1 kHz, so that times in samples equal times in ms. The event is placed at 25 % of the window.
        EventsProvider provider(path("few.xyz.evt"), 1000.0, 25);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        const int reward = provider.eventDescriptionIdMap().value(EventDescription("reward"));

        // The next reward after 250 ms is at 1500 ms: the window starts at 1250 ms.
        const Result next = requestNext(provider, 0, 1000, {reward});
        QCOMPARE(next.name, QString("xyz"));
        QCOMPARE(next.startingTime, 1250_i64);
        QCOMPARE(describe(next), QString("(250, %1)").arg(reward));

        // The previous reward is at 200 ms; the window cannot start before 0.
        const int lick = provider.eventDescriptionIdMap().value(EventDescription("lick"));
        const Result previous = requestPrevious(provider, next.startingTime, 1000, {reward});
        QCOMPARE(previous.startingTime, 0_i64);
        QCOMPARE(describe(previous), QString("(100, %1) (200, %2) (500, %1)").arg(lick).arg(reward));
    }

    // The window Next Event and Previous Event go to holds the events a read of the same window does, also more than
    // 2^24 ms (4.66 h) into a recording.
    void browseToAnEventLateInARecording()
    {
        writeTextFile(path("late.lat.evt"), eventFileContent({{100.0, "lick"}, {17000001.37, "reward"}, {17200000.0, "lick"}}));
        EventsProvider provider(path("late.lat.evt"), 20000.0, 25);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));
        const int reward = provider.eventDescriptionIdMap().value(EventDescription("reward"));

        const Result next = requestNext(provider, 16000000, 1000, {reward});
        QCOMPARE(describe(next), describe(request(provider, next.startingTime, next.startingTime + 1000)));
        const Result previous = requestPrevious(provider, 17100000, 1000, {reward});
        QCOMPARE(describe(previous), describe(request(provider, previous.startingTime, previous.startingTime + 1000)));
    }

    void editOneOfCloseEvents_data()
    {
        QTest::addColumn<QList<Event>>("events");

        // The events at 10.x ms round to the same millisecond.
        QTest::newRow("close events in the middle")
            << QList<Event>{{1.0, "lick"}, {10.1, "lick"}, {10.3, "reward"}, {10.45, "lick"}, {20.0, "reward"}, {30.0, "lick"}};
    }

    // The event removed or moved is the one at the given time, also among events less than a millisecond apart.
    void editOneOfCloseEvents()
    {
        QFETCH(QList<Event>, events);
        const double samplingRate = 20000.0;
        writeTextFile(path("close.cls.evt"), eventFileContent(events));
        for (int edited = 0; edited < events.size(); ++edited)
        {
            // No new time removes the event.
            for (double newTime: {-1.0, 0.5, 15.0, 50.0})
            {
                EventsProvider provider(path("close.cls.evt"), samplingRate);
                QCOMPARE(provider.loadData(), int(EventsProvider::OK));
                QList<Event> expected = events;
                const Event event = expected.takeAt(edited);
                if (newTime < 0)
                {
                    provider.removeEvent(ids[event.description], event.time);
                }
                else
                {
                    provider.modifiedEvent(ids[event.description], event.time, newTime);
                    expected.append({newTime, event.description});
                    std::sort(expected.begin(), expected.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
                }

                QVERIFY2(describe(request(provider, 0, 100)) == expectedWindow(expected, ids, 0, 100, samplingRate),
                         qPrintable(QString("event at %1 ms moved to %2 ms").arg(event.time).arg(newTime)));
            }
        }
    }

    void saveEventsInTimeOrder_data()
    {
        QTest::addColumn<QList<Event>>("events");
        QTest::addColumn<double>("time");
        QTest::addColumn<double>("newTime");
        QTest::addColumn<QList<Event>>("expected");

        // No time adds a lick at the new time.
        QTest::newRow("moved past an event of another description")
            << QList<Event>{{100.0, "lick"}, {500.0, "reward"}, {505.0, "lick"}, {900.0, "lick"}} << 100.0 << 300.0
            << QList<Event>{{300.0, "lick"}, {500.0, "reward"}, {505.0, "lick"}, {900.0, "lick"}};
        QTest::newRow("moved before an event less than a millisecond later")
            << QList<Event>{{100.0, "lick"}, {1000.2, "reward"}, {1000.4, "stim on"}} << 100.0 << 1000.1
            << QList<Event>{{1000.1, "lick"}, {1000.2, "reward"}, {1000.4, "stim on"}};
        QTest::newRow("added before an event less than a millisecond later")
            << QList<Event>{{100.0, "lick"}, {1000.2, "reward"}, {1000.4, "stim on"}} << qQNaN() << 1000.1
            << QList<Event>{{100.0, "lick"}, {1000.1, "lick"}, {1000.2, "reward"}, {1000.4, "stim on"}};
    }

    // A moved or added event is saved in time order.
    void saveEventsInTimeOrder()
    {
        QFETCH(QList<Event>, events);
        QFETCH(double, time);
        QFETCH(double, newTime);
        QFETCH(QList<Event>, expected);
        writeTextFile(path("order.ord.evt"), eventFileContent(events));
        EventsProvider provider(path("order.ord.evt"), 20000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));

        if (qIsNaN(time))
        {
            provider.addEvent("lick", newTime);
        }
        else
        {
            provider.modifiedEvent(ids["lick"], time, newTime);
        }

        QCOMPARE(saved(provider, path("order-saved.ord.evt")), eventFileContent(expected));
    }

    void saveRoundTrip()
    {
        EventsProvider provider(path("many.abc.evt"), 20000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));

        // Opened like NeuroscopeDoc does: without QIODevice::Text, so that the line endings stay \n on Windows.
        QFile file(path("saved.abc.evt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(provider.save(&file));
        file.close();

        QFile original(path("many.abc.evt"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original.readAll());
    }

    void addAndRemoveEvent()
    {
        EventsProvider provider(path("few.xyz.evt"), 1000.0);
        QCOMPARE(provider.loadData(), int(EventsProvider::OK));

        provider.addEvent("new", 700.0);
        QVERIFY(provider.isModified());
        QCOMPARE(provider.getNbEvents(), 6);
        // A new description gets an id in alphabetical order: lick, new, reward.
        QCOMPARE(provider.eventDescriptionIdMap().value(EventDescription("new")), 2);
        QCOMPARE(describe(request(provider, 600, 800)), QString("(100, 2)"));

        provider.undo();
        QCOMPARE(provider.getNbEvents(), 5);
        QCOMPARE(describe(request(provider, 600, 800)), QString());
    }
};

QTEST_GUILESS_MAIN(TestEventsProvider)
#include "test_eventsprovider.moc"
