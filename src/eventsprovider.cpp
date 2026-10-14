/***************************************************************************
                          eventsprovider.cpp  -  description
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
#include <QStringList>
#include <QFileInfo>
#include <QRegularExpression>

#include <QTextStream>
#include <QList>
#include <QDebug>

#include <algorithm>

//include files for the application
#include "eventsprovider.h"
#include "timer.h"
#include "utilities.h"


EventsProvider::EventsProvider(const QString& fileUrl, double currentSamplingRate, int position)
    : DataProvider(fileUrl), nbEvents(0),
      eventPosition(static_cast<float>(position) / 100.0), modified(false)
{

    this->currentSamplingRate = static_cast<double>(currentSamplingRate / 1000.0);

    //Find the event file identifier and use it as the name for the provider
    //the file name is X.id.evt (id is a 3 character identifier)
    QString fileName = fileUrl;
    const int startingIndex = fileName.lastIndexOf("evt");
    if (startingIndex == static_cast<int>(fileName.length()) - 3)
    { //X.id.evt
        int nBStartingIndex = fileName.lastIndexOf(".", startingIndex - 2);
        name = fileName.mid(nBStartingIndex + 1, (startingIndex - 1) - (nBStartingIndex + 1));
    }
    else
    { //X.evt.id
        const int nBStartingIndex = fileName.lastIndexOf(".");
        name = fileName.right(static_cast<int>(fileName.length()) - (nBStartingIndex + 1));
    }
}

EventsProvider::~EventsProvider()
{
    qDebug() << "in ~EventsProvider " << "\n";
}

int EventsProvider::loadData()
{
    RestartTimer();

    //Get the number of events
    nbEvents = Utilities::getNbLines(fileName);

    //qDebug()<<"nbEvents "<<nbEvents<<"\n";

    if (nbEvents == -1)
    {
        events.setSize(0, 0);
        timeStamps.setSize(0, 0);
        return COUNT_ERROR;
    }

    if (nbEvents == 0)
    {
        initializeEmptyProvider();
        return OK;
    }

    //Create a reader on the eventFile
    QFile eventFile(fileName);
    bool status = eventFile.open(QIODevice::ReadOnly);
    if (!status)
    {
        events.setSize(0, 0);
        timeStamps.setSize(0, 0);
        return OPEN_ERROR;
    }

    //Set the size of the Arrays containing the time and ids of the events.
    events.setSize(1, nbEvents);
    timeStamps.setSize(1, nbEvents);

    QTextStream fileStream(&eventFile);
    QString line;
    int lineCounter = 0;
    for (line = fileStream.readLine(); !line.isNull() && lineCounter < nbEvents; line = fileStream.readLine())
    {
        line = line.trimmed();

        int index1 = line.indexOf(QRegularExpression("\\s"));
        int index2 = line.indexOf(QRegularExpression("\\S"), index1);

        timeStamps[lineCounter] = line.left(index1).toDouble();
        EventDescription label = line.right(line.length() - index2);
        events[lineCounter] = label;
        if (eventDescriptionCounter.contains(label))
        {
            eventDescriptionCounter.insert(label, eventDescriptionCounter[label] + 1);
        }
        else
            eventDescriptionCounter.insert(label, 1);
        lineCounter++;
    }

    eventFile.close();
    qDebug() << "Loading evt file into memory: " << Timer() << "\n";


    //The number of events read has to be coherent with the number of events read.
    if (lineCounter != nbEvents)
    {
        events.setSize(0, 0);
        timeStamps.setSize(0, 0);
        return INCORRECT_CONTENT;
    }

    updateMappingAndDescriptionLength();

    return OK;
}


void EventsProvider::updateMappingAndDescriptionLength()
{
    //Assign an id to each event description
    //The iterator iterates on the keys sorted
    QMap<EventDescription, int>::Iterator iterator;
    int id = 1;
    long maxSize = 0;
    long sum = 0;
    long sumOfSquares = 0;
    for (iterator = eventDescriptionCounter.begin(); iterator != eventDescriptionCounter.end(); ++iterator)
    {
        eventIds.insert(iterator.key(), id);
        idsDescriptions.insert(id, iterator.key());
        long length = static_cast<long>(iterator.key().length());
        if (length > maxSize)
            maxSize = length;
        sum += length;
        sumOfSquares += (length * length);
        id++;
    }

    //Compute the length to use to display the descrption of the events in the event palette.
    // descriptionLength = min(mean + 1 * standard deviation, maxsize)
    long mean = sum / eventIds.size();
    //variance(X) = mean(X^2) - mean(X)^2
    long variance = (sumOfSquares / eventIds.size()) - (mean * mean);
    //standard deviation = square root of the variance
    long stdVar = static_cast<long>(sqrt(static_cast<double>(variance)));
    descriptionLength = static_cast<int>(qMin((mean + stdVar), maxSize));
    //Be sure that the length is minimum 2 digits
    descriptionLength = qMax(descriptionLength, 2);
}

void EventsProvider::initializeEmptyProvider()
{
    modified = true;
    nbEvents = 0;
    events.setSize(0, 0);
    timeStamps.setSize(0, 0);
    ///Default description length is 2 characters
    descriptionLength = 2;
}

void EventsProvider::requestData(long startTime, long endTime, QObject* initiator, long startTimeInRecordingUnits)
{
    retrieveData(startTime, endTime, initiator);
}

void EventsProvider::retrieveData(long startTime, long endTime, QObject* initiator)
{
    Array<dataType> times;
    Array<int> ids;
    eventsInWindow(startTime, endTime, times, ids);

    //Send the information to the receiver.
    emit dataReady(times, ids, initiator, name);
}

void EventsProvider::eventsInWindow(long startTime, long endTime, Array<dataType>& times, Array<int>& ids)
{
    //The traces of a window are its samples from startTime to endTime, both included, numbered from 0.
    //An event is drawn on the sample nearest to it, and belongs to the window if that sample does.
    const dataType lastSample = static_cast<dataType>(endTime * currentSamplingRate) - static_cast<dataType>(startTime * currentSamplingRate);
    const long first = firstEventAtOrAfter(startTime - 1.0 / currentSamplingRate);
    const long last = firstEventAfter(endTime + 1.0 / currentSamplingRate) - 1;

    Array<dataType> windowTimes;
    Array<int> windowIds;
    windowTimes.setSize(1, qMax(last - first + 1, 0L));
    windowIds.setSize(1, qMax(last - first + 1, 0L));
    long count = 0;
    for (long i = first; i <= last; ++i)
    {
        const dataType sample = static_cast<dataType>(floor(0.5 + (timeStamps(1, i) - static_cast<double>(startTime)) * currentSamplingRate));
        if (sample >= 0 && sample <= lastSample)
        {
            ++count;
            windowTimes(1, count) = sample;
            windowIds(1, count) = eventIds[events(1, i)];
        }
    }

    times.setSize(1, count);
    ids.setSize(1, count);
    times.copySubset(windowTimes, count);
    ids.copySubset(windowIds, count);
}

long EventsProvider::firstEventAtOrAfter(double time)
{
    double* const begin = nbEvents == 0 ? nullptr : &timeStamps(1, 1);
    return 1 + (std::lower_bound(begin, begin + nbEvents, time) - begin);
}

long EventsProvider::firstEventAfter(double time)
{
    double* const begin = nbEvents == 0 ? nullptr : &timeStamps(1, 1);
    return 1 + (std::upper_bound(begin, begin + nbEvents, time) - begin);
}

void EventsProvider::requestNextEventData(long startTime, long timeFrame, const QList<int>& selectedIds, QObject* initiator)
{
    //A window starts on a whole millisecond, and shows the event browsed to timeFrame * eventPosition after its start, so the
    //events which round to the same millisecond share a window. Browsing goes to the nearest event which rounds to another
    //millisecond than the one at that position, so that the window always moves.
    const long offset = static_cast<long>(timeFrame * eventPosition);
    long index = firstEventAtOrAfter(startTime + offset + 0.5);
    while (index <= nbEvents && !selectedIds.contains(eventIds[events(1, index)]))
    {
        ++index;
    }

    Array<dataType> times;
    Array<int> ids;
    //Without a next event, the start is returned unchanged.
    long startingTime = startTime;
    if (index <= nbEvents)
    {
        startingTime = qMax(static_cast<long>(floor(0.5 + timeStamps(1, index))) - offset, 0L);
        eventsInWindow(startingTime, startingTime + timeFrame, times, ids);
    }
    emit nextEventDataReady(times, ids, initiator, name, startingTime);
}

void EventsProvider::requestPreviousEventData(long startTime, long timeFrame, QList<int> selectedIds, QObject* initiator)
{
    //See requestNextEventData.
    const long offset = static_cast<long>(timeFrame * eventPosition);
    long index = firstEventAtOrAfter(startTime + offset - 0.5) - 1;
    while (index >= 1 && !selectedIds.contains(eventIds[events(1, index)]))
    {
        --index;
    }

    Array<dataType> times;
    Array<int> ids;
    //Without a previous event, the start is returned unchanged.
    long startingTime = startTime;
    if (index >= 1)
    {
        startingTime = qMax(static_cast<long>(floor(0.5 + timeStamps(1, index))) - offset, 0L);
        eventsInWindow(startingTime, startingTime + timeFrame, times, ids);
    }
    emit previousEventDataReady(times, ids, initiator, name, startingTime);
}

void EventsProvider::modifiedEvent(int selectedEventId, double time, double newTime)
{
    modified = true;

    const long timeIndex = findIndex(time, selectedEventId);
    EventDescription selectedEvent = idsDescriptions[selectedEventId];

    //Clear the redo variables
    eventsRedo.setSize(0, 0);
    timeStampsRedo.setSize(0, 0);
    eventDescriptionCounterRedo.clear();

    //Prepare the undo variables
    eventsUndo = events;
    timeStampsUndo = timeStamps;
    eventDescriptionCounterUndo = eventDescriptionCounter;

    //The event goes after the events at its new time, as an added event does.
    if (newTime > timeStamps(1, timeIndex))
    {
        const long newTimeIndex = firstEventAfter(newTime) - 1;
        for (long i = timeIndex; i < newTimeIndex; ++i)
        {
            events(1, i) = events(1, i + 1);
            timeStamps(1, i) = timeStamps(1, i + 1);
        }
        events(1, newTimeIndex) = selectedEvent;
        timeStamps(1, newTimeIndex) = newTime;
    }
    else if (newTime < timeStamps(1, timeIndex))
    {
        const long newTimeIndex = firstEventAfter(newTime);
        for (long i = timeIndex; i > newTimeIndex; --i)
        {
            events(1, i) = events(1, i - 1);
            timeStamps(1, i) = timeStamps(1, i - 1);
        }
        events(1, newTimeIndex) = selectedEvent;
        timeStamps(1, newTimeIndex) = newTime;
    }
}

long EventsProvider::findIndex(double eventTime, int eventId)
{
    long startIndex = qMin(firstEventAtOrAfter(eventTime), nbEvents);

    if (eventId != -1)
    {
        int id = eventIds[events(1, startIndex)];
        if (id != eventId)
        {
            double diff1 = fabs(timeStamps(1, startIndex) - timeStamps(1, startIndex - 1));
            double diff2 = fabs(timeStamps(1, startIndex + 1) - timeStamps(1, startIndex));
            if (diff1 < diff2 && eventIds[events(1, startIndex - 1)] == eventId)
                startIndex--;
            else if (diff1 < diff2 && eventIds[events(1, startIndex - 1)] != eventId && eventIds[events(1, startIndex + 1)] == eventId)
                startIndex++;
            else if (diff2 < diff1 && eventIds[events(1, startIndex + 1)] == eventId)
                startIndex++;
        }
    }

    return startIndex;
}


void EventsProvider::undo()
{
    modified = true; //in case the user saved and then undo, this will allowed to save again

    eventsRedo = events;
    timeStampsRedo = timeStamps;
    eventDescriptionCounterRedo = eventDescriptionCounter;

    events = eventsUndo;
    timeStamps = timeStampsUndo;
    eventDescriptionCounter = eventDescriptionCounterUndo;

    //update the number of events in case some have been removed or added
    nbEvents = events.nbOfColumns();

    //Clear the undo variables
    eventsUndo.setSize(0, 0);
    timeStampsUndo.setSize(0, 0);
    eventDescriptionCounterUndo.clear();

    if (eventDescriptionCounter.count() < eventDescriptionCounterRedo.count())
    {
        QMap<EventDescription, int>::Iterator iterator;
        for (iterator = eventDescriptionCounterRedo.begin(); iterator != eventDescriptionCounterRedo.end(); ++iterator)
        {
            if (!eventDescriptionCounter.contains(iterator.key()))
            {
                removeEventDescription(iterator.key());
                break;
            }
        }
    }
    if (eventDescriptionCounter.count() > eventDescriptionCounterRedo.count())
    {
        QMap<EventDescription, int>::Iterator iterator;
        for (iterator = eventDescriptionCounter.begin(); iterator != eventDescriptionCounter.end(); ++iterator)
        {
            if (!eventDescriptionCounterRedo.contains(iterator.key()))
            {
                addEventDescription(iterator.key());
                break;
            }
        }
    }
}

void EventsProvider::redo()
{
    modified = true; //in case the user saved and then undo, this will allowed to save again

    eventsUndo = events;
    timeStampsUndo = timeStamps;
    eventDescriptionCounterUndo = eventDescriptionCounter;

    events = eventsRedo;
    timeStamps = timeStampsRedo;
    eventDescriptionCounter = eventDescriptionCounterRedo;

    //update the number of events in case some have been removed or added
    nbEvents = events.nbOfColumns();

    //Clear the redo variables
    eventsRedo.setSize(0, 0);
    timeStampsRedo.setSize(0, 0);
    eventDescriptionCounterRedo.clear();

    if (eventDescriptionCounter.count() < eventDescriptionCounterUndo.count())
    {
        QMap<EventDescription, int>::Iterator iterator;
        for (iterator = eventDescriptionCounterUndo.begin(); iterator != eventDescriptionCounterUndo.end(); ++iterator)
        {
            if (!eventDescriptionCounter.contains(iterator.key()))
            {
                removeEventDescription(iterator.key());
                break;
            }
        }
    }
    if (eventDescriptionCounter.count() > eventDescriptionCounterUndo.count())
    {
        QMap<EventDescription, int>::Iterator iterator;
        for (iterator = eventDescriptionCounter.begin(); iterator != eventDescriptionCounter.end(); ++iterator)
        {
            if (!eventDescriptionCounterUndo.contains(iterator.key()))
            {
                addEventDescription(iterator.key());
                break;
            }
        }
    }
}

void EventsProvider::clearUndoRedoData()
{
    //Clear the redo arrays
    eventsRedo.setSize(0, 0);
    timeStampsRedo.setSize(0, 0);
    eventDescriptionCounterRedo.clear();

    //Clear the undo arrays
    eventsUndo.setSize(0, 0);
    timeStampsUndo.setSize(0, 0);
    eventDescriptionCounterUndo.clear();
}

bool EventsProvider::save(QFile* eventFile)
{
    QTextStream fileStream(eventFile);
    fileStream.setRealNumberPrecision(12);

    for (int i = 1; i <= nbEvents; ++i)
        fileStream << timeStamps(1, i) << "\t" << events(1, i) << "\n";

    bool status = eventFile->isOpen();
    if (status)
        modified = false;
    return status;
}


void EventsProvider::removeEvent(int selectedEventId, double time)
{
    modified = true;
    long timeIndex;
    if (nbEvents != 1)
        timeIndex = findIndex(time, selectedEventId);

    //Clear the redo variables
    eventsRedo.setSize(0, 0);
    timeStampsRedo.setSize(0, 0);
    eventDescriptionCounterRedo.clear();

    //Prepare the undo variables
    eventsUndo = events;
    timeStampsUndo = timeStamps;
    eventDescriptionCounterUndo = eventDescriptionCounter;

    EventDescription description = idsDescriptions[selectedEventId];
    eventDescriptionCounter[description] = eventDescriptionCounter[description] - 1;

    if (eventDescriptionCounter[description] == 0)
    {
        removeEventDescription(description);
        eventDescriptionCounter.remove(description);
    }

    if (nbEvents != 1)
    {
        events.setSize(1, nbEvents - 1);
        timeStamps.setSize(1, nbEvents - 1);

        events.copySubset(eventsUndo, 1, (timeIndex - 1), 1);
        events.copySubset(eventsUndo, (timeIndex + 1), nbEvents, timeIndex);
        timeStamps.copySubset(timeStampsUndo, 1, (timeIndex - 1), 1);
        timeStamps.copySubset(timeStampsUndo, (timeIndex + 1), nbEvents, timeIndex);

        nbEvents--;
    }
    else
    {
        events.setSize(0, 0);
        timeStamps.setSize(0, 0);

        nbEvents = 0;
    }
}

void EventsProvider::addEvent(const QString& eventDescriptionToAdd, double time)
{
    modified = true;

    //Clear the redo variables
    eventsRedo.setSize(0, 0);
    timeStampsRedo.setSize(0, 0);
    eventDescriptionCounterRedo.clear();

    //Prepare the undo variables
    eventsUndo = events;
    timeStampsUndo = timeStamps;
    eventDescriptionCounterUndo = eventDescriptionCounter;

    if (nbEvents != 0)
    {
        //The event goes after the events at its time.
        const long timeIndex = firstEventAfter(time);

        events.setSize(1, nbEvents + 1);
        timeStamps.setSize(1, nbEvents + 1);

        events.copySubset(eventsUndo, 1, (timeIndex - 1), 1);
        events(1, timeIndex) = EventDescription(eventDescriptionToAdd);
        events.copySubset(eventsUndo, timeIndex, nbEvents, timeIndex + 1);

        timeStamps.copySubset(timeStampsUndo, 1, (timeIndex - 1), 1);
        timeStamps(1, timeIndex) = time;
        timeStamps.copySubset(timeStampsUndo, timeIndex, nbEvents, timeIndex + 1);
    }
    //New (empty) event file
    else
    {
        events.setSize(1, 1);
        timeStamps.setSize(1, 1);

        events(1, 1) = EventDescription(eventDescriptionToAdd);
        timeStamps(1, 1) = time;
    }

    nbEvents++;

    //Check if the added event description is a new event description.
    bool newDescription = !eventIds.contains(EventDescription(eventDescriptionToAdd));

    //Add the new description to the list of existing ones and compute the new descriptionLength
    if (newDescription)
        addEventDescription(eventDescriptionToAdd);
    else
        eventDescriptionCounter[eventDescriptionToAdd] = eventDescriptionCounter[eventDescriptionToAdd] + 1;
}

void EventsProvider::addEventDescription(QString eventDescriptionToAdd)
{

    QMap<int, int> oldNewEventIds;
    QMap<int, int> newOldEventIds;
    QMap<EventDescription, int> eventIdsTmp;

    //Add the new description to the list of existing ones and compute the new descriptionLength
    idsDescriptions.clear();
    QList<EventDescription> descriptions = eventIds.keys();

    descriptions.append(EventDescription(eventDescriptionToAdd));

    std::sort(descriptions.begin(), descriptions.end());
    long maxSize = 0;
    long sum = 0;
    long sumOfSquares = 0;
    for (int i = 0; i < static_cast<int>(descriptions.size()); ++i)
    {
        EventDescription description = descriptions[i];
        if (description != eventDescriptionToAdd)
        {
            oldNewEventIds.insert(eventIds[description], i + 1);
            newOldEventIds.insert(i + 1, eventIds[description]);
        }
        eventIdsTmp.insert(description, i + 1);
        idsDescriptions.insert(i + 1, description);
        long length = static_cast<long>(description.length());
        if (length > maxSize)
            maxSize = length;
        sum += length;
        sumOfSquares += (length * length);
    }

    //Rebuild eventIds
    eventIds.clear();
    QMap<EventDescription, int>::Iterator it;
    for (it = eventIdsTmp.begin(); it != eventIdsTmp.end(); ++it)
    {
        eventIds.insert(it.key(), it.value());
    }


    //Compute the length to use to display the description of the events in the event palette.
    // descriptionLength = min(mean + 1 * standard deviation, maxsize)
    long mean = sum / eventIds.size();
    //variance(X) = mean(X^2) - mean(X)^2
    long variance = (sumOfSquares / eventIds.size()) - (mean * mean);
    //standard deviation = square root of the variance
    long stdVar = static_cast<long>(sqrt(static_cast<double>(variance)));
    descriptionLength = static_cast<int>(qMin((mean + stdVar), maxSize));
    //Be sure that the length is minimum 2 digits
    descriptionLength = qMax(descriptionLength, 2);

    eventDescriptionCounter.insert(eventDescriptionToAdd, 1);

    emit newEventDescriptionCreated(name, oldNewEventIds, newOldEventIds, eventDescriptionToAdd);
}

void EventsProvider::removeEventDescription(QString eventDescriptionToRemove)
{
    QMap<int, int> oldNewEventIds;
    QMap<int, int> newOldEventIds;
    QMap<EventDescription, int> eventIdsTmp;
    int removedEventId = eventIds[eventDescriptionToRemove];

    //Remove the description of the list of existing ones and compute the new descriptionLength
    idsDescriptions.clear();
    QList<EventDescription> newDescriptions = eventIds.keys();
    newDescriptions.removeAll(EventDescription(eventDescriptionToRemove));

    std::sort(newDescriptions.begin(), newDescriptions.end());
    long maxSize = 0;
    long sum = 0;
    long sumOfSquares = 0;
    for (int i = 0; i < static_cast<int>(newDescriptions.size()); ++i)
    {
        EventDescription description = newDescriptions[i];
        oldNewEventIds.insert(eventIds[description], i + 1);
        newOldEventIds.insert(i + 1, eventIds[description]);
        eventIdsTmp.insert(description, i + 1);
        idsDescriptions.insert(i + 1, description);
        long length = static_cast<long>(description.length());
        if (length > maxSize)
            maxSize = length;
        sum += length;
        sumOfSquares += (length * length);
    }

    //Rebuild eventIds
    eventIds.clear();
    QMap<EventDescription, int>::Iterator it;
    for (it = eventIdsTmp.begin(); it != eventIdsTmp.end(); ++it)
    {
        eventIds.insert(it.key(), it.value());
    }

    //the default length is 2 digits
    if (eventIds.size() == 0)
        descriptionLength = 2;
    else
    {
        //Compute the length to use to display the descrption of the events in the event palette.
        // descriptionLength = min(mean + 1 * standard deviation, maxsize)
        long mean = sum / eventIds.size();
        //variance(X) = mean(X^2) - mean(X)^2
        long variance = (sumOfSquares / eventIds.size()) - (mean * mean);
        //standard deviation = square root of the variance
        long stdVar = static_cast<long>(sqrt(static_cast<double>(variance)));
        descriptionLength = static_cast<int>(qMin((mean + stdVar), maxSize));
        //Be sure that the length is minimum 2 digits
        descriptionLength = qMax(descriptionLength, 2);
    }

    emit eventDescriptionRemoved(name, oldNewEventIds, newOldEventIds, removedEventId, eventDescriptionToRemove);
}

void EventsProvider::renameEvent(int selectedEventId, const QString& newEventDescription, double time)
{
    modified = true;

    //Clear the redo variables
    eventsRedo.setSize(0, 0);
    timeStampsRedo.setSize(0, 0);
    eventDescriptionCounterRedo.clear();

    //Prepare the undo variables
    eventsUndo = events;
    timeStampsUndo = timeStamps;
    eventDescriptionCounterUndo = eventDescriptionCounter;

    //remove the old event description
    EventDescription description = idsDescriptions[selectedEventId];
    eventDescriptionCounter[description] = eventDescriptionCounter[description] - 1;

    if (eventDescriptionCounter[description] == 0)
    {
        removeEventDescription(description);
        eventDescriptionCounter.remove(description);
    }

    //replace the old description by the new one.
    if (nbEvents != 0)
    {
        long timeIndex = findIndex(time);

        events(1, timeIndex) = EventDescription(newEventDescription);
    }
    //New (empty) event file, should never be possible
    else
        return;


    //Check if the newEventDescription is a new event description.
    bool newDescription = !eventIds.contains(EventDescription(newEventDescription));

    //Add the new description to the list of existing ones and compute the new descriptionLength
    if (newDescription)
        addEventDescription(newEventDescription);
    else
        eventDescriptionCounter[newEventDescription] = eventDescriptionCounter[newEventDescription] + 1;
}


//Operator < on EventDescription to sort them in an case-insensitive maner.
bool operator<(const EventDescription& s1, const EventDescription& s2)
{
    if (s1.toLower() == s2.toLower())
        return (static_cast<QString>(s1) < static_cast<QString>(s2));
    else
        return (static_cast<QString>(s1.toLower()) < static_cast<QString>(s2.toLower()));
}
