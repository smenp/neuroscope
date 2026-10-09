/***************************************************************************
                          test_recordingend.cpp  -  description
                             -------------------
    purpose              : Tests for moving the trace view to the end of
                           a recording
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
#include "tracesprovider.h"
#include "tracewidget.h"

#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 2;
const long WINDOW = 100;
// Every sample is 0 but the last one, so that the last sample shown tells which sample it is.
const std::int16_t LAST_SAMPLE = 1000;
// Gain of the 16-bit recordings of parameterFile(), in microvolts per unit.
const double GAIN = 20.0 * 1000000 / (65536.0 * 1000);

enum class Move
{
    ScrollBarToMaximum,
    EndKey,
    PageDown,
    TypedStartTime,
    AutoAdvance,
};

} // namespace

Q_DECLARE_METATYPE(Move)

class TestRecordingEnd : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir dir;

    QString recording(double samplingRate, std::int64_t nbSamples)
    {
        const QString baseName = QString("rec-%1-%2").arg(samplingRate).arg(nbSamples);
        const QString datPath = QDir(dir.path()).filePath(baseName + ".dat");
        if (!QFileInfo::exists(datPath))
        {
            QByteArray samples(nbSamples * NB_CHANNELS * sizeof(std::int16_t), 0);
            for (int channel = 0; channel < NB_CHANNELS; ++channel)
                std::memcpy(samples.data() + ((nbSamples - 1) * NB_CHANNELS + channel) * sizeof(std::int16_t), &LAST_SAMPLE, sizeof(LAST_SAMPLE));
            writeFile(datPath, samples);
            writeTextFile(QDir(dir.path()).filePath(baseName + ".xml"), parameterFile(NB_CHANNELS, samplingRate));
        }
        return datPath;
    }

    static void move(TraceWidget* widget, Move how)
    {
        QScrollBar* scrollBar = widget->findChild<QScrollBar*>();
        QSpinBox* milliseconds = nullptr;
        for (QSpinBox* box: widget->findChildren<QSpinBox*>())
            if (box->suffix().trimmed() == "ms")
                milliseconds = box;
        QVERIFY(scrollBar && milliseconds);
        const long length = scrollBar->maximum() + WINDOW;

        switch (how)
        {
        case Move::ScrollBarToMaximum:
            scrollBar->setValue(scrollBar->maximum());
            break;
        case Move::EndKey:
            QTest::keyClick(scrollBar, Qt::Key_End);
            break;
        case Move::PageDown:
            widget->slotSetStartAndDuration(length - 3 * WINDOW - WINDOW / 2, WINDOW);
            for (int page = 0; page < 5; ++page)
                QTest::keyClick(scrollBar, Qt::Key_PageDown);
            break;
        case Move::TypedStartTime:
            milliseconds->selectAll();
            QTest::keyClicks(milliseconds, QString::number(milliseconds->maximum()));
            QTest::keyClick(milliseconds, Qt::Key_Return);
            break;
        case Move::AutoAdvance:
            widget->advance();
            widget->stop();
            break;
        }
    }

  private Q_SLOTS:
    void initTestCase()
    {
        isolateSettings();
        QVERIFY(dir.isValid());
    }

    // Moving to the end of a recording shows a window that ends with its last sample, without an error.
    void moveToTheEnd_data()
    {
        QTest::addColumn<double>("samplingRate");
        QTest::addColumn<std::int64_t>("nbSamples");
        QTest::addColumn<Move>("how");

        const QList<QPair<double, std::int64_t>> recordings = {
            // The last sample lies inside the last millisecond.
            { 1250, 777 },
            // The last sample lies at a whole millisecond.
            { 20000, 200001 },
            { 30000, 1234567 },
            // Durations whose milliseconds are not exact in single precision.
            { 1250, 10520644 },
            { 20000, 10555539 },
        };
        const QList<QPair<const char*, Move>> moves = {
            { "scroll bar to maximum", Move::ScrollBarToMaximum },
            { "End key", Move::EndKey },
            { "Page Down", Move::PageDown },
            { "typed start time", Move::TypedStartTime },
            { "auto-advance", Move::AutoAdvance },
        };
        for (const auto& recording: recordings)
            for (const auto& move: moves)
                QTest::addRow("%g Hz, %lld samples, %s", recording.first, static_cast<long long>(recording.second), move.first)
                    << recording.first << recording.second << move.second;
    }

    void moveToTheEnd()
    {
        QFETCH(double, samplingRate);
        QFETCH(std::int64_t, nbSamples);
        QFETCH(Move, how);

        auto app = openRecording(recording(samplingRate, nbSamples));
        TraceWidget* widget = app->findChild<TraceWidget*>();
        QVERIFY(widget);
        widget->slotSetStartAndDuration(0, WINDOW);

        MessageBoxRecorder messages;
        move(widget, how);
        if (QTest::currentTestFailed())
            return;
        QCOMPARE(messages.texts, QStringList());

        TracesProvider& provider = app->getDocument()->tracesDataProvider();
        const long start = app->activeView()->getStartTime();
        QCOMPARE(app->activeView()->getTimeWindow(), WINDOW);
        QCOMPARE(static_cast<long long>(start + WINDOW), static_cast<long long>(provider.recordingLength()));

        Matrix shown;
        QObject requester;
        QObject::connect(&provider, &TracesProvider::dataReady, &requester,
                         [&](Array<dataType>& data, QObject* initiator)
                         {
                             if (initiator == &requester)
                                 shown = toMatrix(data);
                         });
        provider.requestData(start, start + WINDOW, &requester, 0);
        QCOMPARE(shown.rows, static_cast<std::int64_t>(provider.getNbSamples(start, start + WINDOW, 0)));
        QVERIFY(shown.rows > 1);
        for (int channel = 1; channel <= NB_CHANNELS; ++channel)
        {
            QCOMPARE(shown(shown.rows - 1, channel), 0_i64);
            QCOMPARE(shown(shown.rows, channel), roundHalfAway(LAST_SAMPLE * GAIN));
        }
    }
};

QTEST_MAIN(TestRecordingEnd)
#include "test_recordingend.moc"
