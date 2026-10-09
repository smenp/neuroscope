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
};

QTEST_MAIN(TestNeuroscopeDoc)
#include "test_neuroscopedoc.moc"
