/***************************************************************************
                          channelmimedata.h
                             -------------------
    begin                : 24/10/2013
    copyright            : (C) 2013 by David Faure
    email                : david.faure@kdab.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef CHANNELMIMEDATA_H
#define CHANNELMIMEDATA_H

#include <QMimeData>

class ChannelMimeData : public QMimeData
{
  public:
    /**Sets the id of the group dragged.*/
    void setGroup(int groupId);

    static bool hasGroup(const QMimeData* mimeData);
    static int group(const QMimeData* mimeData);
};

#endif
