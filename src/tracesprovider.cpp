/***************************************************************************
                          tracesprovider.cpp  -  description
                             -------------------
    begin                : Mon Mar 1 2004
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

//C include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64


//include files for the application
#include "tracesprovider.h"

#include <QFile>
#include <QRegularExpression>
#include <QDebug>
#include <QFileInfo>

// include c/c++ headers
#include <stdint.h>

//include files for c/c++ libraries
#include <math.h>

#include <algorithm>

namespace
{
// Neuralynx .ncs files: a text header, then records of a header (timestamp, channel number, sampling
// frequency and number of valid samples) and a fixed number of samples.
const int NCS_HEADER_SIZE = 16 * 1024;
const int NCS_RECORD_HEADER_SIZE = 20;
const int NCS_SAMPLES_PER_RECORD = 512;
} // namespace

TracesProvider::TracesProvider(const QString& fileUrl, int nbChannels, int resolution, int voltageRange, int amplification, double samplingRate, int offset)
    : DataProvider(fileUrl),
      nbChannels(nbChannels),
      resolution(resolution),
      voltageRange(voltageRange),
      amplification(amplification),
      samplingRate(samplingRate),
      offset(offset)
{
    computeRecordingLength();
}

TracesProvider::~TracesProvider()
{
}


dataType TracesProvider::getNbSamples(long startTime, long endTime, long startTimeInRecordingUnits)
{
    const QPair<dataType, dataType> samples = samplesInWindow(startTime, endTime, startTimeInRecordingUnits);
    return samples.second - samples.first + 1;
}

QPair<dataType, dataType> TracesProvider::samplesInWindow(long startTime, long endTime, long startTimeInRecordingUnits) const
{
    //Convert the time in miliseconds to time in recording units if need it.
    //startTimeInRecordingUnits has been computed in a previous call to a clustersProvider browsing function. It has to be used insted of computing
    //the value from startTime because of the rounding which has been applied to it.
    const double samplesPerMillisecond = samplingRate / 1000.0;
    const dataType first = startTimeInRecordingUnits != 0 ? startTimeInRecordingUnits : static_cast<dataType>(startTime * samplesPerMillisecond);

    //The caller should have check that we do not go over the end of the file.
    //The length of the recording is truncated to whole miliseconds, so the sample at the end time of a window ending
    //at the length may lie past the end of the file, and samples may lie between the length and the end of the file.
    //Such a window ends with the last sample of the file.
    const dataType last = endTime == length ? lastSample : static_cast<dataType>(endTime * samplesPerMillisecond);
    return qMakePair(first, last);
}

void TracesProvider::requestData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits)
{
    retrieveData(startTime, endTime, initiator, startTimeInRecordingUnits);
}

void TracesProvider::retrieveData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits)
{
    Array<dataType> data;
    //When the bug in gcc will be corrected for the 64 bits, the c++ code will be use
    //[alex@slut]/home/alex/src/sizetest > ./sizetest-2.95.3
    //  sizeof(std::streamoff) = 8 bytes (64 bits)
    //  sizeof(std::streampos) = 8 bytes (64 bits)

    // [alex@slut]/home/alex/src/sizetest > ./sizetest-3.2.2
    //  sizeof(std::streamoff) = 4 bytes (32 bits)
    //  sizeof(std::streampos) = 12 bytes (96 bits)

    /*  ifstream dataFile;

 //Open the file containing the data
 dataFile.open(fileName,ifstream::in|ifstream::binary);
 if(dataFile.fail()){
  //emit the signal with an empty array, the reciever will take care of it, given a message to the user.
  data.setSize(0,0);
  emit dataReady(data,initiator);
  return;
 }*/

    const QPair<dataType, dataType> samples = samplesInWindow(startTime, endTime, startTimeInRecordingUnits);
    const dataType startInRecordingUnits = samples.first;
    const dataType nbSamples = samples.second - samples.first + 1;

    //data will contain the final values.
    data.setSize(nbSamples, nbChannels);

    // Compute acquisition gain
    double acquisitionGain = (voltageRange * 1000000) / (pow(2.0, resolution) * amplification);

    //Depending on the acquisition system resolution, the data are store as short or long
    if ((resolution == 12) | (resolution == 14) | (resolution == 16))
    {
        Array<int16_t> retrieveData(nbSamples, nbChannels);
        qint64 nbValues = nbSamples * nbChannels;
        // Is this a Neuralynx file?
        int p = fileName.lastIndexOf(".ncs");
        if (p != -1)
        {
            qDebug() << "NCS";
            /// Modified by M.Zugaro to read Neuralynx ncs format

            const qint64 recordSize = NCS_RECORD_HEADER_SIZE + NCS_SAMPLES_PER_RECORD * sizeof(int16_t);
            int16_t buffer[NCS_SAMPLES_PER_RECORD];

            int p = fileName.lastIndexOf(".");
            QString baseName = fileName;
            baseName.truncate(p - 1);
            p = baseName.lastIndexOf(QRegularExpression("[^0-9]"));
            baseName.truncate(p + 1);

            for (int channel = 1; channel <= nbChannels; ++channel)
            {
                // Open CSC file
                QString cscFileName;
                FILE* dataFile;
                for (int i = 0; i <= 3; ++i)
                {
                    // Files are numbered 1...N but we do not know if they are zero-padded,
                    // so we try different padding lengths (from 0 to 3 digits)
                    QString pad;
                    for (int j = 0; j < i; ++j)
                        pad += "0";
                    cscFileName = baseName + pad + QString::fromLatin1("%1.ncs").arg(channel);
                    dataFile = fopen(cscFileName.toLatin1(), "rb");
                    if (dataFile != NULL)
                        break;
                }
                if (dataFile == NULL)
                {
                    // Emit the signal with an empty array, let the receiver handle the error (user message).
                    data.setSize(0, 0);
                    emit dataReady(data, initiator);
                    return;
                }

                // Read the window record by record, skipping the record headers. Samples past the end of the
                // file are 0, because the channel files do not necessarily have the same number of records.
                for (dataType i = 0; i < nbSamples;)
                {
                    const dataType sample = startInRecordingUnits + i;
                    const dataType record = sample / NCS_SAMPLES_PER_RECORD;
                    const int offsetInRecord = static_cast<int>(sample - record * NCS_SAMPLES_PER_RECORD);
                    const int nbToRead = static_cast<int>(qMin<dataType>(NCS_SAMPLES_PER_RECORD - offsetInRecord, nbSamples - i));

                    fseeko64(dataFile, NCS_HEADER_SIZE + record * recordSize + NCS_RECORD_HEADER_SIZE + offsetInRecord * sizeof(int16_t), SEEK_SET);
                    const size_t nbRead = fread(buffer, sizeof(int16_t), nbToRead, dataFile);
                    std::fill(buffer + nbRead, buffer + nbToRead, 0);
                    for (int j = 0; j < nbToRead; ++j, ++i)
                        retrieveData[i * nbChannels + channel - 1] = buffer[j];
                }
                fclose(dataFile);
            }
            /// (end of code modified by M.Zugaro)
        }
        else
        {
            QFile dataFile(fileName);
            if (!dataFile.open(QIODevice::ReadOnly))
            {
                data.setSize(0, 0);
                emit dataReady(data, initiator);
                return;
            }

            qint64 position = static_cast<qint64>(static_cast<qint64>(startInRecordingUnits) * static_cast<qint64>(nbChannels));

            dataFile.seek(position * sizeof(int16_t));
            qint64 nbRead = dataFile.read(reinterpret_cast<char*>(&retrieveData[0]), sizeof(int16_t) * nbValues);

            // copy the data into retrieveData.
            if (nbRead != qint64(nbValues * sizeof(int16_t)))
            {
                //emit the signal with an empty array, the reciever will take care of it, given a message to the user.
                data.setSize(0, 0);
                dataFile.close();
                emit dataReady(data, initiator);
                return;
            }
            dataFile.close();
        }
        //Subtract the offset, convert to microvolts and store the values in data.
        for (qint64 i = 0; i < nbValues; ++i)
            data[i] = round((static_cast<dataType>(retrieveData[i]) - offset) * acquisitionGain);
    }
    else if (resolution == 32)
    {

        QFile dataFile(fileName);
        if (!dataFile.open(QIODevice::ReadOnly))
        {
            data.setSize(0, 0);
            emit dataReady(data, initiator);
            return;
        }
        Array<int32_t> retrieveData(nbSamples, nbChannels);
        qint64 nbValues = nbSamples * nbChannels;
        qint64 position = static_cast<qint64>(static_cast<qint64>(startInRecordingUnits) * static_cast<qint64>(nbChannels));

        dataFile.seek(position * sizeof(int32_t));
        qint64 nbRead = dataFile.read(reinterpret_cast<char*>(&retrieveData[0]), sizeof(int32_t) * nbValues);

        // copy the data into retrieveData.
        if (nbRead != qint64(nbValues * sizeof(int32_t)))
        {
            //emit the signal with an empty array, the reciever will take care of it, given a message to the user.
            data.setSize(0, 0);
            dataFile.close();
            emit dataReady(data, initiator);
            return;
        }
        //Subtract the offset, convert to microvolts and store the values in data.
        for (qint64 i = 0; i < nbValues; ++i)
            data[i] = round((static_cast<dataType>(retrieveData[i]) - offset) * acquisitionGain);
        //The data have been retrieve, close the file.
        dataFile.close();
    }

    //Send the information to the receiver.
    emit dataReady(data, initiator);
}

void TracesProvider::computeRecordingLength()
{
    //When the bug in gcc will be corrected for the 64 bits the c++ code will be use
    //[alex@slut]/home/alex/src/sizetest > ./sizetest-2.95.3
    //  sizeof(std::streamoff) = 8 bytes (64 bits)
    //  sizeof(std::streampos) = 8 bytes (64 bits)

    // [alex@slut]/home/alex/src/sizetest > ./sizetest-3.2.2
    //  sizeof(std::streamoff) = 4 bytes (32 bits)
    //  sizeof(std::streampos) = 12 bytes (96 bits)

    /*ifstream dataFile;

 //Open the file containing the data
 dataFile.open(fileName,ifstream::in|ifstream::binary);
 if(dataFile.fail()) return 0;

 // get the length of the file:
 dataFile.seekg (0, ios::end);
 qint64 fileLength = dataFile.tellg();
 dataFile.close();*/

    length = 0;
    lastSample = -1;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
        return;
    f.close();
    QFileInfo fInfo(fileName);
    qint64 fileLength = fInfo.size();

    int dataSize = 0;
    if ((resolution == 12) | (resolution == 14) | (resolution == 16))
        dataSize = 2;
    else if (resolution == 32)
        dataSize = 4;
    if (dataSize == 0 || nbChannels <= 0 || samplingRate <= 0)
        return;

    qint64 nbSamples;
    // Is this a Neuralynx file?
    int p = fileName.lastIndexOf(".ncs");
    if (p != -1)
    {
        /// Modified by M.Zugaro to read Neuralynx ncs format

        int recordSize = NCS_RECORD_HEADER_SIZE + NCS_SAMPLES_PER_RECORD * dataSize;

        // Determine number of complete records in file, + amount of extra data (last record may be incomplete)
        int64_t nRecords = (fileLength - NCS_HEADER_SIZE) / recordSize;
        int extraData = (fileLength - NCS_HEADER_SIZE) - nRecords * recordSize - NCS_RECORD_HEADER_SIZE;
        if (extraData < 0)
            extraData = 0;
        // Only one channel per file!
        nbSamples = nRecords * NCS_SAMPLES_PER_RECORD + extraData / dataSize;
        /// (end of code modified by M.Zugaro)
    }
    else
        nbSamples = fileLength / (static_cast<qint64>(nbChannels) * dataSize);

    lastSample = nbSamples - 1;
    // In double precision: in single precision, the length of a recording of a few minutes can already be rounded past its end.
    length = static_cast<qlonglong>(static_cast<double>(nbSamples) * 1000.0 / samplingRate);
}

long TracesProvider::getTotalNbSamples()
{
    return static_cast<long>((length * samplingRate) / 1000);
}

QStringList TracesProvider::getLabels()
{
    QStringList labels;

    for (int i = 0; i < this->nbChannels; i++)
    {
        labels << QString::number(i);
    }

    return labels;
}
