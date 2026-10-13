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

#include <QAction>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 4;
const double SAMPLING_RATE = 20000;
const std::int64_t NB_SAMPLES = 20000;

/** Closes and records every message box shown while it exists. */
class MessageBoxRecorder
{
  public:
    MessageBoxRecorder()
    {
        QObject::connect(&timer, &QTimer::timeout, [this]
                         {
            if (QMessageBox* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                texts.append(box->text());
                box->done(QMessageBox::Ok);
            } });
        timer.start(0);
    }

    QStringList texts;

  private:
    QTimer timer;
};

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
        writeTextFile(recordingDir.filePath("session.abc.evt"), "10\tstim\n50\tstim\n");
        MessageBoxRecorder messages;
        {
            auto app = testutils::openRecording(datPath);
            loadFile(app.get(), "slotLoadClusterFiles", writeClusterFile(recordingDir, "session", 1, { { 100, 1 }, { 2000, 2 }, { 5000, 1 } }));
            loadFile(app.get(), "slotLoadEventFiles", recordingDir.filePath("session.abc.evt"));
            QMetaObject::invokeMethod(app.get(), "saveSession");
        }
        QVERIFY(QFileInfo::exists(recordingDir.filePath("session.nrs")));

        auto app = testutils::openRecording(datPath);

        QVERIFY(app->findChild<QWidget*>("clusterPanel"));
        QVERIFY(app->findChild<QWidget*>("eventPanel"));
        QCOMPARE(messages.texts, QStringList());
    }

    // The video rotation and flip saved in the parameter file are those of the opened recording.
    void readTheVideoSettingsOfTheParameterFile()
    {
        const QDir recordingDir(dir.path());
        const QString datPath = writeRecording(recordingDir, "video", NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        writeTextFile(recordingDir.filePath("video.xml"),
                      parameterFile(NB_CHANNELS, SAMPLING_RATE)
                          .replace("</parameters>", "<neuroscope version=\"2.0.0\"><video><rotate>90</rotate><flip>1</flip></video></neuroscope></parameters>"));

        auto app = testutils::openRecording(datPath);

        QCOMPARE(app->getDocument()->getRotation(), 90);
        QCOMPARE(app->getDocument()->getFlip(), 1);
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

    void closeWithTheFocusInTheStartTime_data()
    {
        QTest::addColumn<QString>("closing");
        QTest::newRow("close the window") << "window";
        QTest::newRow("quit") << "quit";
        QTest::newRow("close the only display") << "display";
    }

    // Closing takes the keyboard focus from the start time box, which then applies the digits typed in it.
    void closeWithTheFocusInTheStartTime()
    {
        QFETCH(QString, closing);
        const QString datPath = writeRecording(QDir(dir.path()), QString("close-") + QTest::currentDataTag(), 1, 1250, 200 * 1250);
        auto app = testutils::openRecording(datPath);
        app->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(app.get()));
        QSpinBox* seconds = nullptr;
        for (QSpinBox* box: app->findChild<TraceWidget*>()->findChildren<QSpinBox*>())
        {
            if (box->suffix().trimmed() == "s")
            {
                seconds = box;
            }
        }
        QVERIFY(seconds);
        QLineEdit* edit = seconds->findChild<QLineEdit*>();
        QTest::mouseClick(edit, Qt::LeftButton);
        QVERIFY(seconds->hasFocus());
        QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(edit, "5");

        if (closing == "window")
        {
            QVERIFY(app->close());
        }
        else if (closing == "quit")
        {
            // File > Quit, through its action: a platform may bind no key to Quit.
            QAction* quit = nullptr;
            for (QAction* action: app->findChildren<QAction*>())
            {
                if (action->text() == "Quit")
                {
                    quit = action;
                }
            }
            QVERIFY(quit);
            quit->trigger();
        }
        else
        {
            QTest::keySequence(edit, QKeySequence(Qt::CTRL | Qt::Key_W));
        }

        QVERIFY(!app->getDocument()->isADocumentToClose());
    }

    void restoreTheSelections_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::addColumn<bool>("moved");
        QTest::newRow("paths") << "" << false;
    }

    // Every display of a session shows and browses the clusters and events it was saved with.
    void restoreTheSelections()
    {
        QFETCH(QString, scheme);
        QFETCH(bool, moved);
        const QDir recordingDir(dir.filePath(QString("selections-%1").arg(QTest::currentDataTag()).replace(' ', '-')));
        QVERIFY(recordingDir.mkpath("."));
        const QString datPath = writeRecording(recordingDir, "selections", NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        const QString clusterPath = writeClusterFile(recordingDir, "selections", 1, { { 100, 1 }, { 2000, 2 }, { 5000, 3 } });
        const QString eventPath = recordingDir.filePath("selections.abc.evt");
        writeTextFile(eventPath, "10\tlick\n20\treward\n");
        const QString savedDir = moved ? recordingDir.filePath("moved-away") : recordingDir.absolutePath();
        const auto saved = [&](const QString& path) -> QString { return scheme + savedDir + "/" + QFileInfo(path).fileName(); };
        const auto file = [](int type, const QString& url, const QString& path, const QStringList& items) -> QString
        {
            QString file = QString("<file><type>%1</type><url>%2</url><modificationDate>%3</modificationDate><items>")
                               .arg(type)
                               .arg(url, QFileInfo(path).lastModified().toString(Qt::ISODate));
            for (const QString& item: items)
                file += QString("<itemDescription><item>%1</item><color>#ff0000</color></itemDescription>").arg(item);
            return file + "</items></file>";
        };
        // The structure of the session files of NeuroScope 1.3: clusters 2 and 3 and events 1 and 2 are shown,
        // cluster 3 and event 2 are not browsed.
        const QString display = QString("<display><clustersSelected><fileUrl>%1</fileUrl><cluster>2</cluster><cluster>3</cluster></clustersSelected>"
                                        "<eventsSelected><fileUrl>%2</fileUrl><event>1</event><event>2</event></eventsSelected>"
                                        "<clustersSkipped><fileUrl>%1</fileUrl><cluster>3</cluster></clustersSkipped>"
                                        "<eventsSkipped><fileUrl>%2</fileUrl><event>2</event></eventsSkipped></display>")
                                    .arg(saved(clusterPath), saved(eventPath));
        writeTextFile(recordingDir.filePath("selections.nrs"),
                      QString("<?xml version='1.0'?><neuroscope version=\"1.3.3\"><files>%1%2</files><displays>%3%3</displays></neuroscope>")
                          .arg(file(0, saved(clusterPath), clusterPath, { "1", "2", "3" }), file(2, saved(eventPath), eventPath, { "lick", "reward" }), display));
        MessageBoxRecorder messages;

        auto app = testutils::openRecording(datPath);

        QVERIFY2(messages.texts.isEmpty(), qPrintable(messages.texts.join('\n')));
        const QList<NeuroscopeView*> views = app->findChildren<NeuroscopeView*>();
        QCOMPARE(views.size(), 2);
        for (NeuroscopeView* view: views)
        {
            QCOMPARE(*view->getSelectedClusters("1"), QList<int>({ 2, 3 }));
            // Clusters that are not shown are not browsed either.
            QCOMPARE(*view->getClustersNotUsedForBrowsing("1"), QList<int>({ 1, 3 }));
            QCOMPARE(*view->getSelectedEvents("abc"), QList<int>({ 1, 2 }));
            QCOMPARE(*view->getEventsNotUsedForBrowsing("abc"), QList<int>({ 2 }));
        }
    }
};

QTEST_MAIN(TestNeuroscopeDoc)
#include "test_neuroscopedoc.moc"
