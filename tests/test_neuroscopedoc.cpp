/***************************************************************************
                          test_neuroscopedoc.cpp  -  description
                             -------------------
    purpose              : Tests for the document of an opened recording
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

#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 4;
const double SAMPLING_RATE = 20000;
const std::int64_t NB_SAMPLES = 20000;

} // namespace

class TestNeuroscopeDoc : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir dir;

  private Q_SLOTS:
    void initTestCase()
    {
        isolateSettings();
        QVERIFY(dir.isValid());
    }

    void openRecording()
    {
        const QString datPath = writeRecording(QDir(dir.path()), "open", NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        auto app = testutils::openRecording(datPath);
        NeuroscopeDoc* doc = app->getDocument();
        QCOMPARE(doc->url(), datPath);
        QCOMPARE(doc->getChannelLabels()->size(), NB_CHANNELS);
    }

    void changeTheChannelCount_data()
    {
        QTest::addColumn<int>("nbChannels");
        QTest::newRow("more") << NB_CHANNELS + 3;
        QTest::newRow("fewer") << NB_CHANNELS - 1;
    }

    // The channel palettes label every channel of the new count.
    void changeTheChannelCount()
    {
        QFETCH(int, nbChannels);
        const QString datPath = writeRecording(QDir(dir.path()), QString("count-") + QTest::currentDataTag(), NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        auto app = testutils::openRecording(datPath);
        NeuroscopeDoc* doc = app->getDocument();

        doc->setChannelNb(nbChannels);

        QCOMPARE(doc->getChannelNb(), nbChannels);
        QCOMPARE(doc->getChannelLabels()->size(), nbChannels);
    }

    // A session remembers when its files were last modified; reopening it with unchanged files does not warn.
    void reopenASessionWithUnchangedFiles()
    {
        const QDir recordingDir(dir.path());
        const QString datPath = writeRecording(recordingDir, "session", NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        writeTextFile(recordingDir.filePath("session.clu.1"), "2\n1\n2\n1\n");
        writeTextFile(recordingDir.filePath("session.res.1"), "100\n2000\n5000\n");
        writeTextFile(recordingDir.filePath("session.abc.evt"), "10\tstim\n50\tstim\n");
        MessageBoxRecorder messages;
        {
            auto app = testutils::openRecording(datPath);
            loadFile(app.get(), "slotLoadClusterFiles", recordingDir.filePath("session.clu.1"));
            loadFile(app.get(), "slotLoadEventFiles", recordingDir.filePath("session.abc.evt"));
            QMetaObject::invokeMethod(app.get(), "saveSession");
        }
        QVERIFY(QFileInfo::exists(recordingDir.filePath("session.nrs")));
        QCOMPARE(messages.texts, QStringList());

        auto app = testutils::openRecording(datPath);

        QVERIFY(app->findChild<QWidget*>("clusterPanel"));
        QVERIFY(app->findChild<QWidget*>("eventPanel"));
        QCOMPARE(messages.texts, QStringList());
    }

    // The duration can be typed up to the length of the recording.
    void typeALongDuration()
    {
        const double samplingRate = 1250;
        const long duration = 123456;
        const QString datPath = writeRecording(QDir(dir.path()), "long", 1, samplingRate, 200 * 1250);
        auto app = testutils::openRecording(datPath);
        QLineEdit* durationEdit = app->findChild<TraceWidget*>()->findChild<QLineEdit*>();
        QVERIFY(durationEdit);

        durationEdit->clear();
        QTest::keyClicks(durationEdit, QString::number(duration));
        QTest::keyClick(durationEdit, Qt::Key_Return);

        QCOMPARE(app->activeView()->getTimeWindow(), duration);
    }

    // The file dialogs open next in the directory of the last file opened.
    void rememberTheDirectoryOfTheLastFile()
    {
        const QDir recordingDir(dir.path());
        const QString datPath = writeRecording(recordingDir, "directory", NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        const QString eventPath = recordingDir.filePath("directory.abc.evt");
        writeTextFile(eventPath, "10\tstim\n");
        auto app = testutils::openRecording(datPath);

        loadFile(app.get(), "slotLoadEventFiles", eventPath);

        QCOMPARE(QSettings().value("CurrentDirectory").toString(), recordingDir.absolutePath());
    }
};

QTEST_MAIN(TestNeuroscopeDoc)
#include "test_neuroscopedoc.moc"
