/***************************************************************************
                          textvalues.h  -  description
                             -------------------
    purpose              : Reads numeric values from text files
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

#ifndef TEXTVALUES_H
#define TEXTVALUES_H

#include "types.h"

#include <QByteArrayView>

#include <cmath>
#include <limits>
#include <optional>

/**
 * Calls onValue(QByteArrayView value) for every value in @p text, a value being a run of the
 * characters 0-9, e, E, +, - and . (any other character separates values), and onLineEnd() at
 * every '\n' and at the end of a last line without newline. onLineEnd() is called for empty lines
 * too.
 *
 * @return false as soon as a callback returns false, true otherwise.
 */
template<typename OnValue, typename OnLineEnd>
bool forEachTextValue(QByteArrayView text, OnValue onValue, OnLineEnd onLineEnd)
{
    qsizetype valueStart = -1;
    const qsizetype end = text.isEmpty() || text.back() == '\n' ? text.size() : text.size() + 1;
    for (qsizetype i = 0; i < end; ++i)
    {
        const char c = i < text.size() ? text[i] : '\n';
        if ((c >= '0' && c <= '9') || c == 'e' || c == 'E' || c == '+' || c == '-' || c == '.')
        {
            if (valueStart < 0)
                valueStart = i;
            continue;
        }
        if (valueStart >= 0)
        {
            if (!onValue(text.sliced(valueStart, i - valueStart)))
                return false;
            valueStart = -1;
        }
        if (c == '\n' && !onLineEnd())
            return false;
    }
    return true;
}

/** forEachTextValue() for files without a line structure. */
template<typename OnValue>
bool forEachTextValue(QByteArrayView text, OnValue onValue)
{
    return forEachTextValue(text, onValue, []
                            { return true; });
}

/** @p value rounded half up, or nothing if the result is not in the range of dataType or @p value is NaN. */
inline std::optional<dataType> roundToDataType(double value)
{
    const double rounded = std::floor(0.5 + value);
    //-min() is a power of two, exact as a double unlike max().
    const double lowest = static_cast<double>(std::numeric_limits<dataType>::min());
    if (!(rounded >= lowest && rounded < -lowest))
        return std::nullopt;
    return static_cast<dataType>(rounded);
}

#endif
