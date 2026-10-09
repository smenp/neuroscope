/***************************************************************************
                          test_events.cpp  -  description
                             -------------------
    purpose              : Tests for editing events in the trace view
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
#include <QTemporaryDir>
#include <QtTest>

using namespace testutils;

namespace
{

const int NB_CHANNELS = 4;
const double SAMPLING_RATE = 20000;
const std::int64_t NB_SAMPLES = 20000;
const QString DESCRIPTION = "stim";

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
        const QString eventPath = dir.filePath(baseName + ".abc.evt");
        writeTextFile(eventPath, "100\t" + DESCRIPTION + "\n500\t" + DESCRIPTION + "\n");
        auto app = openRecording(datPath);
        loadFile(app.get(), "slotLoadEventFiles", eventPath);
        return app;
    }

    /** Chooses @p description in the Add Event menu, which starts the add-event mode. */
    static void chooseEventToAdd(NeuroscopeApp* app, const QString& description)
    {
        QAction action(description);
        QMetaObject::invokeMethod(app, "slotAddEventButtonActivated", Q_ARG(QAction*, &action));
    }

    // The displays are the pages of the stacked widget of the display tabs.
    static QTabWidget* displayTabsOf(NeuroscopeView* view) { return qobject_cast<QTabWidget*>(view->parentWidget()->parentWidget()); }

    static void clickInTheTraces(NeuroscopeApp* app)
    {
        TraceView* view = app->activeView()->findChild<TraceView*>();
        QVERIFY(view);
        const QPoint middle = view->rect().center();
        QTest::mouseMove(view, middle);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, middle);
    }

  private Q_SLOTS:
    void initTestCase()
    {
        isolateSettings();
        QVERIFY(dir.isValid());
    }

    // Closing the event file that events are added to leaves the add-event mode as the Select tool does.
    void closeTheFileEventsAreAddedTo()
    {
        auto app = openWithEvents("closed");
        chooseEventToAdd(app.get(), DESCRIPTION);
        QVERIFY(app->activeView()->isAddingEventsTo("abc"));

        QVERIFY(closeFileOfPalette(app.get(), "eventPanel", "slotCloseEventFile"));
        QVERIFY(!app->findChild<QWidget*>("eventPanel"));
        QVERIFY(!app->activeView()->isAddingEventsTo("abc"));
        QVERIFY(app->activeView()->isSelectionTool());
        clickInTheTraces(app.get());
    }

    void closeTheFileEventsAreAddedToAmongOthers()
    {
        auto app = openWithEvents("several");
        const QString otherEventPath = dir.filePath("several.def.evt");
        writeTextFile(otherEventPath, "900\t" + DESCRIPTION + "\n");
        loadFile(app.get(), "slotLoadEventFiles", otherEventPath);
        ItemPalette* eventPalette = app->findChild<ItemPalette*>("eventPanel");
        QVERIFY(eventPalette);
        eventPalette->selectGroup("abc");
        const QString added = "added";
        chooseEventToAdd(app.get(), added);
        QVERIFY(app->activeView()->isAddingEventsTo("abc"));

        QVERIFY(closeFileOfPalette(app.get(), "eventPanel", "slotCloseEventFile"));
        QVERIFY(!app->activeView()->isAddingEventsTo("abc"));
        QVERIFY(!app->activeView()->isAddingEventsTo("def"));
        QVERIFY(app->activeView()->isSelectionTool());
        clickInTheTraces(app.get());
        QVERIFY(!app->getDocument()->eventIds("def").contains(added));
    }

    void closeTheFileAnInactiveViewAddsEventsTo()
    {
        auto app = openWithEvents("inactive");
        NeuroscopeView* adding = app->activeView();
        chooseEventToAdd(app.get(), DESCRIPTION);
        QMetaObject::invokeMethod(app.get(), "slotNewDisplay");
        QTabWidget* displayTabs = displayTabsOf(adding);
        QVERIFY(displayTabs);
        QCOMPARE(displayTabs->count(), 2);
        displayTabs->setCurrentIndex(1);
        QVERIFY(app->activeView() != adding);

        QVERIFY(closeFileOfPalette(app.get(), "eventPanel", "slotCloseEventFile"));
        QVERIFY(!adding->isAddingEventsTo("abc"));
        QVERIFY(adding->isSelectionTool());
        displayTabs->setCurrentWidget(adding);
        QCOMPARE(app->activeView(), adding);
        clickInTheTraces(app.get());
    }
};

QTEST_MAIN(TestEvents)
#include "test_events.moc"
