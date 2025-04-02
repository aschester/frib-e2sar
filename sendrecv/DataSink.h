/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2015

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Jeromy Tompkins
             NSCL
             Michigan State University
             East Lansing, MI 48824-1321
*/

#ifndef DATASINK_H
#define DATASINK_H

/**
 * @file DataSink.h
 * @brief Abstract base class for data sinks.
 */

#include <stdlib.h>

struct iovec;
namespace ufmt {
    class CRingItem;
}

/** 
 * @brief Interface for DataSinks
 * @details
 * This is a pure virtual base class that establishes an
 * expected interface for all data sinks.
 */

class DataSink
{    
public:    
    /** The virtual destructor */
    virtual ~DataSink();

    /**
     * @brief A method defining how to send ring items to the sink
     * @param item References the ring item to put into the sink
     */
    virtual void putItem(const ufmt::CRingItem& item) = 0;
    
    /**
     * @brief Write a block of data to the sink
     * @param pData  Pointer to start of contiguous data to write
     * @param nBytes Number of bytes to write
     */
    virtual void put(const void* pData, size_t nBytes) = 0;

    virtual void putItemsV(iovec* iovs, size_t iovcnt) {};
    virtual void putV(iovec* iovs, size_t iovcnt) {};

};

#endif
