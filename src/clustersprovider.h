/***************************************************************************
                          clustersprovider.h  -  description
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

#ifndef CLUSTERSPROVIDER_H
#define CLUSTERSPROVIDER_H

//include files for the application
#include <dataprovider.h>
#include <array.h>
#include <types.h>

// include files for QT
#include <QObject>

#include <QList>

//include files for c/c++ libraries
#include <math.h>

/**Class storing and providing the cluster information.
  *@author Lynn Hazan
  */

class ClustersProvider : public DataProvider
{
    Q_OBJECT
  public:
    /**Information retun after a call to openFile/saveDocument/createFeatureFile*/
    enum loadReturnMessage
    {
        OK = 0,
        OPEN_ERROR = 1,
        MISSING_FILE = 3,
        COUNT_ERROR = 4,
        INCORRECT_CONTENT = 5
    };

    /**Constructor.
  * @param fileUrl the url of the cluster file containing the cluster ids.
  * @param samplingRate sampling rate used to record the data (of the acquisition system).
  * @param currentSamplingRate sampling rate of the current file.
  * @param fileMaxTime maximum time of the current file in recording units.
  * @param position represents the percentage from the begining of the window where the clusters are display when browsing.
  */
    explicit ClustersProvider(const QString& fileUrl, double samplingRate, double currentSamplingRate, dataType fileMaxTime, int position = 25);
    ~ClustersProvider();

    /**Triggers the retrieve of the cluster information included in the time interval given by @p startTime and @p endTime.
  * @param startTime begining of the time interval from which to retrieve the data in miliseconds.
  * @param endTime end of the time interval from which to retrieve  the data.
  * @param initiator instance requesting the data.
  * @param startTimeInRecordingUnits begining of the time interval from which to retrieve the data in recording units.
  */
    virtual void requestData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits);


    /**Looks up for the first of the clusters included in the list @p selectedIds existing after the time @p startTime.
  * All the clusters included in the time interval given by @p timeFrame are retrieved. The time interval start time is
  * computed in order to have the first cluster found located at @p clusterPosition percentage of the time interval.
  * @param startTime starting time, in miliseconds, for the look up.
  * @param timeFrame time interval for which to retrieve  the data.
  * @param selectedIds list of cluster ids to look up for.
  * @param initiator instance requesting the data.
  * @param startTimeInRecordingUnits starting time, in recording units, for the look up.
  */
    virtual void requestNextClusterData(long startTime, long timeFrame, const QList<int>& selectedIds, QObject* initiator, long startTimeInRecordingUnits);


    /**Looks up for the first of the clusters included in the list @p selectedIds existing before the time @p endTime.
  * All the clusters included in the time interval given by @p timeFrame are retrieved. The time interval start time is
  * computed in order to have the first cluster found located at @p clusterPosition percentage of the time interval.
  * @param startTime starting time, in miliseconds, for the look up.
  * @param timeFrame time interval for which to retrieve  the data.
  * @param selectedIds list of cluster ids to look up for.
  * @param initiator instance requesting the data.
  * @param startTimeInRecordingUnits starting time, in recording units, for the look up.
  */
    virtual void requestPreviousClusterData(long startTime, long timeFrame, QList<int> selectedIds, QObject* initiator, long startTimeInRecordingUnits);

    /**Loads the cluster ids and the corresponding spike time.
  * @return an loadReturnMessage enum giving the load status
  */
    virtual int loadData();

    /**Returns list of cluster Ids.
  * @return */
    inline QList<int> clusterIdList() const { return clusterIds; }

    /**Returns the name of the provider which is the cluster file number.
  * @return provider'name.
  */
    inline QString getName() const { return name; }

    /** Sets the position where the clusters are display when browsing.
  * @param position percentage from the begining of the window.
  */
    void setClusterPosition(int position) { clusterPosition = static_cast<float>(position) / 100.0; }

    /**Updates the sampling rate for the current document.
  * @param rate sampling rate.
  */
    void updateSamplingRate(double rate)
    {
        dataCurrentRatio = static_cast<float>(samplingRate / rate);
    }

    /**Updates the acquisition system sampling rate.
  * @param rate acquisition system sampling rate.
  * @param currentSamplingRate sampling rate for the current document.
  */
    void updateAcquisitionSystemSamplingRate(double rate, double currentSamplingRate)
    {
        samplingRate = rate;
        dataCurrentRatio = static_cast<float>(samplingRate / currentSamplingRate);
        updateFileMaxTime();
    }

  Q_SIGNALS:
    /**Signals that the data have been retrieved.
  * @param data 2 line array containing the sample index of the peak index of each spike existing in the requested time frame with the
  * corresponding cluster id. The first line contains the sample index and the second line the cluster id.
  * @param initiator instance requesting the data.
  * @param providerName name of the instance providing the data.
  */
    void dataReady(Array<dataType>& data, QObject* initiator, QString providerName);

    /**Signals that the data for the next cluster have been retrieved.
  * @param data 2 line array containing the sample index of the peak index of each spike existing in the requested time frame with the
  * corresponding cluster id. The first line contains the sample index and the second line the cluster id.
  * @param initiator instance requesting the data.
  * @param providerName name of the instance providing the data.
  * @param startingTime time from which the data have been retrieved in miliseconds.
  * @param startingTimeInRecordingUnits time from which the data have been retrieved in recording units.
  */
    void nextClusterDataReady(Array<dataType>& data, QObject* initiator, QString providerName, long startingTime, long startingTimeInRecordingUnits);

    /**Signals that the data for the previous cluster have been retrieved.
  * @param data 2 line array containing the sample index of the peak index of each spike existing in the requested time frame with the
  * corresponding cluster id. The first line contains the sample index and the second line the cluster id.
  * @param initiator instance requesting the data.
  * @param providerName name of the instance providing the data.
  * @param startingTime time from which the data have been retrieved.
  * @param startingTimeInRecordingUnits time from which the data have been retrieved in recording units.
  */
    void previousClusterDataReady(Array<dataType>& data, QObject* initiator, QString providerName, long startingTime, long startingTimeInRecordingUnits);

  protected:
    /**Provider's name.*/
    QString name;

    /**Url of the spike time file (.res file).*/
    QString timeFileUrl;

    /**Sampling rate used to record the data.*/
    double samplingRate;

    /**A 2 line array containing the cluster ids and the spikes time.*/
    Array<dataType> clusters;

    /**Number of spikes.*/
    long nbSpikes;

    /**Number of clusters in the cluster file the provider provides the data for.*/
    int nbClusters;

    /**Ratio between the sampling rate of the dat file and the one of the current file.*/
    float dataCurrentRatio;

    /**List of the cluster ids.*/
    QList<int> clusterIds;

    /**The maximum time, in miliseconds, contained in the file.*/
    long fileMaxTime;

    /**Represents the percentage from the begining of the window where the clusters are display when browsing.*/
    float clusterPosition;

    /**The maximum time of the data file in recording units.*/
    dataType dataFileMaxTime;

    //Functions

    /**Retrieves the peak index of each spike included in the time frame given by @p startTime and @p endTime.
  * @param startTime begining of the time frame from which to retrieve the data, given in milisecond.
  * @param endTime end of the time frame from which to retrieve the data, given in milisecond.
  * @param initiator instance requesting the data.
  * @param startTimeInRecordingUnits begining of the time interval from which to retrieve  the data in recording units.
  */
    void retrieveData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits);

    /**Sets fileMaxTime from the time of the last spike.*/
    void updateFileMaxTime()
    {
        fileMaxTime = static_cast<long>(floor(0.5 + clusters(2, nbSpikes) * 1000.0 / samplingRate));
    }

    /**Returns the first sample of a window in recording units.
  * @param startTime beginning of the window in milliseconds.
  * @param startTimeInRecordingUnits beginning of the window at the sampling rate of the current file, 0 if not known.
  */
    dataType windowStart(long startTime, long startTimeInRecordingUnits) const;

    /**Gets the spikes from @p start to @p end, in recording units.
  * @param start first sample of the window.
  * @param end last sample of the window.
  * @param data receives the sample of each spike from @p start, at the sampling rate of the current file, and its cluster id.
  */
    void spikesInWindow(dataType start, dataType end, Array<dataType>& data);

    /**Returns the index of the first spike at or after @p time, in recording units, nbSpikes + 1 if there is none.
  * @param time time in recording units.
  */
    long firstSpikeAtOrAfter(dataType time);

    /**Returns the index of the first spike after @p time, in recording units, nbSpikes + 1 if there is none.
  * @param time time in recording units.
  */
    long firstSpikeAfter(dataType time);

    /**Finds the next or previous spike of a cluster in @p selectedIds and the window showing it.
  * @param startTime beginning of the current window in milliseconds.
  * @param timeFrame duration of the window in milliseconds.
  * @param selectedIds ids of the clusters to browse.
  * @param startTimeInRecordingUnits beginning of the current window at the sampling rate of the current file, 0 if not known.
  * @param forward true for the next spike, false for the previous one.
  * @param data receives the spikes of the window, as spikesInWindow() gives them.
  * @param startingTime receives the beginning of the window in milliseconds, @p startTime if there is no spike.
  * @param startingTimeInRecordingUnits receives the beginning of the window at the sampling rate of the current file,
  * @p startTimeInRecordingUnits if there is no spike.
  */
    void browse(long startTime, long timeFrame, const QList<int>& selectedIds, long startTimeInRecordingUnits, bool forward,
                Array<dataType>& data, long& startingTime, long& startingTimeInRecordingUnits);
};


#endif
