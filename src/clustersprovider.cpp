/***************************************************************************
                          clustersprovider.cpp  -  description
                             -------------------
    begin                : Wed Apr 14 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan.myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
//QT include files
#include <QFile>
#include <QStringList>
#include <QFileInfo>

#include <QList>
#include <QMap>
#include <QDebug>

#include <algorithm>
#include <limits>

//include files for the application
#include "clustersprovider.h"
#include "timer.h"
#include "utilities.h"


ClustersProvider::ClustersProvider(const QString& fileUrl, double samplingRate, double currentSamplingRate, dataType fileMaxTime, int position)
    : DataProvider(fileUrl), timeFileUrl(fileUrl),
      samplingRate(samplingRate), nbSpikes(0), nbClusters(0), clusterPosition(static_cast<float>(position) / 100.0)
{

    dataCurrentRatio = static_cast<float>(samplingRate / currentSamplingRate);
    dataFileMaxTime = fileMaxTime * dataCurrentRatio;

    //Find the cluster file number and use it as the name for the provider
    //the file name can be X.clu.n or X.n.clu
    QString fileName = fileUrl;
    int startingIndex = fileName.lastIndexOf("clu");
    if (startingIndex == static_cast<int>(fileName.length()) - 3)
    { //X.n.clu
        int nBStartingIndex = fileName.lastIndexOf(".", startingIndex - 2);
        name = fileName.mid(nBStartingIndex + 1, (startingIndex - 1) - (nBStartingIndex + 1));
    }
    else
    { //X.clu.n
        int nBStartingIndex = fileName.lastIndexOf(".");
        name = fileName.right(static_cast<int>(fileName.length()) - (nBStartingIndex + 1));
    }

    fileName = fileName.replace(startingIndex, 3, "res");
    timeFileUrl = (fileName);
}

ClustersProvider::~ClustersProvider()
{
}

int ClustersProvider::loadData()
{

    //Fist check if the time file (.res) exists
    QString timeFilePath = timeFileUrl;
    if (!QFile(timeFileUrl).exists())
    {
        clusters.setSize(0, 0);
        return MISSING_FILE;
    }

    //Get the number of spikes
    nbSpikes = Utilities::getNbLines(timeFilePath);

    qDebug() << "nbSpikes " << nbSpikes;

    if (nbSpikes == -1)
    {
        clusters.setSize(0, 0);
        return COUNT_ERROR;
    }

    //should not happen, but just in case
    if (nbSpikes == 0)
    {
        clusters.setSize(0, 0);
        updateFileMaxTime();

        return OK;
    }

    RestartTimer();

    //Create a reader on the clusterFile
    QFile clusterFile(fileName);
    bool status = clusterFile.open(QIODevice::ReadOnly);
    if (!status)
    {
        clusters.setSize(0, 0);
        return OPEN_ERROR;
    }

    //Set the size of the Array containing the spikes and clusters ids.
    clusters.setSize(2, nbSpikes);

    //Get the number of clusters stored on the first line.
    QString sNbClusters;
    QByteArray buf;
    buf.resize(255);
    int ret = clusterFile.readLine(buf.data(), 255);
    sNbClusters = QString::fromLatin1(buf, ret);


    nbClusters = sNbClusters.toInt();
    //This map is used as an easy way to compute the unique list of cluster ids
    QMap<int, long> ids;
    QByteArray buffer = clusterFile.readAll();
    uint size = buffer.size();

    //The buffer is read and each dataType is build char by char into a string. When the char read
    //is not [1-9] (<=> blank space or a new line), the string is converted into a dataType and store
    //into the first row of clusters.
    //string of character which will contains the current seek dataType
    dataType k = 0;
    int l = 0;
    char clusterID[255];
    for (uint i = 0; i < size; ++i)
    {
        if (buffer[i] >= '0' && buffer[i] <= '9')
        {
            clusterID[l++] = buffer[i];
        }
        else if (l)
        {
            clusterID[l] = '\0';
            long id = atol(clusterID);
            clusters[k++] = id; //Warning if the typedef dataType changes, change will have to be made here.
            ids.insert(static_cast<int>(id), id);
            l = 0;
        }
    }

    clusterFile.close();
    clusterIds = ids.keys();

    //The number of spikes read has to be coherent with the number of spikes computed by wc -l (via Utilities::getNbLines).
    if (k != nbSpikes)
    {
        clusters.setSize(0, 0);
        return INCORRECT_CONTENT;
    }

    //Get the spikes time

    //Create a reader on the spikeFile
    QFile spikeFile(timeFilePath);
    status = spikeFile.open(QIODevice::ReadOnly);
    if (!status)
    {
        clusters.setSize(0, 0);
        return OPEN_ERROR;
    }


    QByteArray spikeBuffer = spikeFile.readAll();
    size = spikeBuffer.size();

    //The buffer is read and each dataType is build char by char into a string. When the char read
    //is not [1-9] (<=> blank space or a new line), the string is converted into a dataType and store
    //into the second row of clusters.
    //string of character which will contains the current seek dataType
    l = 0;
    char time[255];
    for (uint i = 0; i < size; ++i)
    {
        if (spikeBuffer[i] >= '0' && spikeBuffer[i] <= '9')
            time[l++] = spikeBuffer[i];
        else if (l)
        {
            time[l] = '\0';
            clusters[k++] = atol(time); //Warning if the typedef dataType changes, change will have to be made here.
            l = 0;
        }
    }

    spikeFile.close();

    //The number of spikes read has to be coherent with the number of spikes computed by wc -l (via Utilities::getNbLines).
    if (k != (2 * nbSpikes))
    {
        clusters.setSize(0, 0);
        return INCORRECT_CONTENT;
    }

    qDebug() << "Loading clu file into memory: " << Timer() << "\n";

    updateFileMaxTime();

    return OK;
}

void ClustersProvider::requestData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits)
{
    retrieveData(startTime, endTime, initiator, startTimeInRecordingUnits);
}

void ClustersProvider::retrieveData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits)
{
    //A window reaching the time of the last spike, rounded to the millisecond, ends with it.
    const dataType end = endTime >= fileMaxTime ? std::numeric_limits<dataType>::max() : static_cast<dataType>(endTime * (samplingRate / 1000.0));
    Array<dataType> data;
    spikesInWindow(windowStart(startTime, startTimeInRecordingUnits), end, data);

    //Send the information to the receiver.
    emit dataReady(data, initiator, name);
}

dataType ClustersProvider::windowStart(long startTime, long startTimeInRecordingUnits) const
{
    //After spike browsing, startTimeInRecordingUnits is the start of the window at the sampling rate of the current file,
    //which startTime only gives rounded to the millisecond.
    if (startTimeInRecordingUnits != 0)
    {
        return static_cast<dataType>(floor(0.5 + startTimeInRecordingUnits * static_cast<double>(dataCurrentRatio)));
    }
    return static_cast<dataType>(startTime * (samplingRate / 1000.0));
}

void ClustersProvider::spikesInWindow(dataType start, dataType end, Array<dataType>& data)
{
    const long first = firstSpikeAtOrAfter(start);
    const long count = qMax(firstSpikeAfter(end) - first, 0L);
    data.setSize(2, count); //line 1 sample index in the current file, line 2 cluster id
    for (long i = 1; i <= count; ++i)
    {
        data(1, i) = static_cast<dataType>(floor(0.5 + (clusters(2, first + i - 1) - start) / static_cast<double>(dataCurrentRatio)));
        data(2, i) = clusters(1, first + i - 1);
    }
}

long ClustersProvider::firstSpikeAtOrAfter(dataType time)
{
    dataType* const begin = nbSpikes == 0 ? nullptr : &clusters(2, 1);
    return 1 + (std::lower_bound(begin, begin + nbSpikes, time) - begin);
}

long ClustersProvider::firstSpikeAfter(dataType time)
{
    dataType* const begin = nbSpikes == 0 ? nullptr : &clusters(2, 1);
    return 1 + (std::upper_bound(begin, begin + nbSpikes, time) - begin);
}

void ClustersProvider::browse(long startTime, long timeFrame, const QList<int>& selectedIds, long startTimeInRecordingUnits, bool forward,
                              Array<dataType>& data, long& startingTime, long& startingTimeInRecordingUnits)
{
    //The spike browsed to is placed clusterPosition into the window, and the window is identified by its first sample in the
    //current file. Browsing goes to the nearest spike whose window starts after, or before, the current one, so that the
    //window always moves.
    const dataType timeFrameInRecordingUnits = static_cast<dataType>(timeFrame * (samplingRate / 1000.0));
    const long position = static_cast<long>(static_cast<float>(timeFrameInRecordingUnits) * clusterPosition);
    const auto startOfWindow = [&](dataType spikeTime)
    {
        dataType start = qMax(spikeTime - position, 0L);
        //Going forward, the window keeps its length at the end of the recording.
        if (forward && start + timeFrameInRecordingUnits > dataFileMaxTime)
        {
            start = dataFileMaxTime - timeFrameInRecordingUnits;
        }
        return start;
    };
    const auto inCurrentFile = [&](dataType time) { return static_cast<dataType>(floor(0.5 + time / static_cast<double>(dataCurrentRatio))); };
    const dataType current = inCurrentFile(windowStart(startTime, startTimeInRecordingUnits));

    dataType* const begin = nbSpikes == 0 ? nullptr : &clusters(2, 1);
    long index = 1 + (std::partition_point(begin, begin + nbSpikes, [&](dataType time)
                                           { return forward ? inCurrentFile(startOfWindow(time)) <= current : inCurrentFile(startOfWindow(time)) < current; }) -
                      begin);
    const long step = forward ? 1 : -1;
    if (!forward)
    {
        --index;
    }
    while (index >= 1 && index <= nbSpikes && !selectedIds.contains(clusters(1, index)))
    {
        index += step;
    }

    //Without a spike, the start is returned unchanged.
    startingTime = startTime;
    startingTimeInRecordingUnits = startTimeInRecordingUnits;
    if (index < 1 || index > nbSpikes)
    {
        return;
    }
    const dataType start = startOfWindow(clusters(2, index));
    startingTime = static_cast<long>(floor(0.5 + start * 1000.0 / samplingRate));
    startingTimeInRecordingUnits = inCurrentFile(start);
    //The window shown starts on a sample of the current file.
    const dataType shownStart = windowStart(startingTime, startingTimeInRecordingUnits);
    spikesInWindow(shownStart, start + timeFrameInRecordingUnits, data);
}

void ClustersProvider::requestNextClusterData(long startTime, long timeFrame, const QList<int>& selectedIds, QObject* initiator, long startTimeInRecordingUnits)
{
    Array<dataType> data;
    long startingTime;
    long startingTimeInRecordingUnits;
    browse(startTime, timeFrame, selectedIds, startTimeInRecordingUnits, true, data, startingTime, startingTimeInRecordingUnits);
    emit nextClusterDataReady(data, initiator, name, startingTime, startingTimeInRecordingUnits);
}


void ClustersProvider::requestPreviousClusterData(long startTime, long timeFrame, QList<int> selectedIds, QObject* initiator, long startTimeInRecordingUnits)
{
    Array<dataType> data;
    long startingTime;
    long startingTimeInRecordingUnits;
    browse(startTime, timeFrame, selectedIds, startTimeInRecordingUnits, false, data, startingTime, startingTimeInRecordingUnits);
    emit previousClusterDataReady(data, initiator, name, startingTime, startingTimeInRecordingUnits);
}
