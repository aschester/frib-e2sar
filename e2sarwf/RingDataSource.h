/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2017.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Authors:
             Ron Fox
             Giordano Cerriza
	     FRIB
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

/** 
 * @file  RingDataSource.h
 * @brief Provide ring items from a ringbuffer
 */

#ifndef RINGDATASOURCE_H
#define RINGDATASOURCE_H

#include "DataSource.h"

#include <climits>

class CRingBuffer;

/**
 * @class RingDataSource
 * @brief This class provides a data source to get items from a ringbuffer.
 */

class RingDataSource : public DataSource
{
private:
    CRingBuffer& m_ring; //!< References the ringbuffer we get data from
public:
    /**
     * @brief Constructor
     * @param pFact Factory we use to get items.
     * @param ring  References the ring buffer from which rings come.
     */
    RingDataSource(ufmt::RingItemFactoryBase* pFact, CRingBuffer& ring);
    /** @brief Destructor */
    virtual ~RingDataSource() {};
    
    /**
     * @brief Get the next item from the ring buffer
     * @return Pointer to next ring item - cannot be nullptr!
     */
    virtual ufmt::CRingItem* getItem();
    /**
     * @brief Get the next item from the ring buffer
     * @param timeout Seconds to wait for data
     * @return Pointer to next ring item or nullptr if timed out
     */
    ufmt::CRingItem* getItem(unsigned long timeout);

};

#endif
