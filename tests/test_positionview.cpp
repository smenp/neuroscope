/***************************************************************************
                          test_positionview.cpp  -  description
                             -------------------
    purpose              : Tests for the position view
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
#include "positionview.h"
#include "tracewidget.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

class TestPositionView : public QObject
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

    // Events of the shown window after the end of the position file are not drawn in the position view.
    void eventsAfterTheLastPosition()
    {
        const QDir recordingDir(dir.path());
        const QString datPath = writeRecording(recordingDir, "short", 4, 20000, 200000);
        // One second of positions at the default rate of 39.0625 Hz, for a recording of 10 s.
        QString positions;
        for (int line = 0; line < 39; ++line)
            positions += QString("%1 %2\n").arg(10 + line).arg(20 + line);
        writeTextFile(recordingDir.filePath("short.pos"), positions);
        writeTextFile(recordingDir.filePath("short.abc.evt"), "700\tstim\n1500\tstim\n");
        MessageBoxRecorder messages;
        auto app = openRecording(datPath);
        loadFile(app.get(), "slotLoadEventFiles", recordingDir.filePath("short.abc.evt"));
        loadFile(app.get(), "slotLoadPositionFile", recordingDir.filePath("short.pos"));
        QVERIFY(app->findChild<PositionView*>());
        app->activeView()->shownEventsUpdate("abc", { 1 });
        app->activeView()->setEventsInPositionView(true);

        QMetaObject::invokeMethod(app->findChild<TraceWidget*>(), "slotSetStartAndDuration", Q_ARG(long, 500), Q_ARG(long, 2000));
        app->findChild<PositionView*>()->grab();

        QCOMPARE(messages.texts, QStringList());
    }
};

QTEST_MAIN(TestPositionView)
#include "test_positionview.moc"
