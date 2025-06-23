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
    CRingBuffer&  m_ring;    //!< References the ringbuffer we get data from
    unsigned long m_timeout; // Timeout seconds for reads from ringbuffer
    
public:
    /**
     * @brief Constructor
     * @param pFact   Factory we use to get items.
     * @param ring    References the ring buffer from which items come
     * @param timeout Timeout seconds for reading from ringbuffer
     */
    RingDataSource(ufmt::RingItemFactoryBase* pFact, CRingBuffer& ring,
		   unsigned long timeout=ULONG_MAX);
    /** @brief Destructor */
    virtual ~RingDataSource() {};
    
    /**
     * @brief Get the next item from the ring buffer
     * @return Pointer to next ring item - cannot be nullptr!
     */
    virtual ufmt::CRingItem* getItem();

    /**
     * @brief Set the timeout value
     * @param timeout Seconds to wait for data
     */
    void setTimeout(unsigned long timeout) { m_timeout = timeout; };
    /**
     * @brief Get the timeout value
     * @return Timeout seconds
     */
    unsigned long getTimeout() { return m_timeout; };
};

#endif
