/***************************************************************************
                          test_events.cpp  -  description
                             -------------------
    purpose              : Tests for the events in the trace and position
                           views
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
#include "itempalette.h"
#include "neuroscopeview.h"
#include "traceview.h"

#include <QAction>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 4;
const double SAMPLING_RATE = 20000;
const std::int64_t NB_SAMPLES = 20000;
const QString DESCRIPTION = "stim";

/** Shows the event palette and calls slotCloseEventFile(), as Close Event File does for the file selected in it. */
void closeSelectedEventFile(NeuroscopeApp* app)
{
    QWidget* palette = app->findChild<QWidget*>("eventPanel");
    QVERIFY(palette);
    // The palettes are the pages of the stacked widget of the palette tabs.
    qobject_cast<QTabWidget*>(palette->parentWidget()->parentWidget())->setCurrentWidget(palette);
    QMetaObject::invokeMethod(app, "slotCloseEventFile");
}

void clickInTheTraces(NeuroscopeApp* app)
{
    TraceView* view = app->activeView()->findChild<TraceView*>();
    QVERIFY(view);
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->rect().center());
}

} // namespace

class TestEvents : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir dir;

    /** Opens a recording with the event file @p baseName.abc.evt loaded. */
    std::unique_ptr<NeuroscopeApp> openWithEvents(const QString& baseName)
    {
        const QString datPath = writeRecording(QDir(dir.path()), baseName, NB_CHANNELS, SAMPLING_RATE, NB_SAMPLES);
        auto app = openRecording(datPath);
        writeTextFile(dir.filePath(baseName + ".abc.evt"), "5\t" + DESCRIPTION + "\n500\t" + DESCRIPTION + "\n");
        loadFile(app.get(), "slotLoadEventFiles", dir.filePath(baseName + ".abc.evt"));
        return app;
    }

  private Q_SLOTS:
    void initTestCase()
    {
        isolateSettings();
        QVERIFY(dir.isValid());
    }

    void closeTheFileEventsAreAddedTo_data()
    {
        QTest::addColumn<bool>("inactiveView");
        QTest::newRow("only file") << false;
        QTest::newRow("added to in an inactive view") << true;
    }

    // Closing the event file that events are added to leaves the add-event mode, as the Select tool does.
    void closeTheFileEventsAreAddedTo()
    {
        QFETCH(bool, inactiveView);
        auto app = openWithEvents(QString("adding-%1").arg(QTest::currentDataTag()).replace(' ', '-'));
        app->findChild<ItemPalette*>("eventPanel")->selectGroup("abc");
        NeuroscopeView* adding = app->activeView();
        const QString added = "added";
        QAction choice(added);
        QMetaObject::invokeMethod(app.get(), "slotAddEventButtonActivated", Q_ARG(QAction*, &choice));
        // The displays are the pages of the stacked widget of the display tabs.
        QTabWidget* displayTabs = qobject_cast<QTabWidget*>(adding->parentWidget()->parentWidget());
        if (inactiveView)
        {
            QMetaObject::invokeMethod(app.get(), "slotNewDisplay");
            displayTabs->setCurrentIndex(1);
            QVERIFY(app->activeView() != adding);
        }

        closeSelectedEventFile(app.get());

        displayTabs->setCurrentWidget(adding);
        clickInTheTraces(app.get());
        QVERIFY(adding->isSelectionTool());
    }
};

QTEST_MAIN(TestEvents)
#include "test_events.moc"
