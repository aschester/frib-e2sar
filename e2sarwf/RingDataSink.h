/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2015

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Jeromy Tompkins 
	     Aaron Chester
	     NSCL/FRIB
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

/**
 * @file RingDataSink.h
 * @brief Define a ringbuffer data sink for NSCLDAQ dataflow
 */

#ifndef RINGDATASINK_H
#define RINGDATASINK_H

#include "DataSink.h"

#include <string>

class CRingBuffer;

/**
 * @class RingDataSink
 * @brief Ringbuffer data sink for NSCLDAQ dataflows
 */

class RingDataSink : public DataSink
{
  private:
    CRingBuffer* m_pRing;    //!< Pointer to ringbuffer we're writing to
    std::string  m_ringName; //!< Ringbuffer name
 
  public:
    /**
     * @brief Constructor
     * @param ringName Name of the output ringbuffer
     */
    RingDataSink(std::string ringName);
    /** @brief Destructor */
    virtual ~RingDataSink();

  private:
    /**
     * @name DeletedOperations
     * @brief Copy construction, assignment, etc. are not sensible for
     * ringbuffer data sinks and are private NOOPs
     */
     /**@{*/
    RingDataSink(const RingDataSink& rhs);
    RingDataSink& operator=(const RingDataSink& rhs);
    int operator==(const RingDataSink& rhs) const;
    int operator!=(const RingDataSink& rhs) const;
    /**@}*/

public:    
    /**
     * @brief Puts a ring item in the sink (ring)
     * @param item Reference to the item to put
     */
    virtual void putItem(const ufmt::CRingItem& item);
    /**
     * @brief Puts arbitrary data to the sink (ring)
     * @param pData Pointer to the buffer holding the data
     * @param nBytes Number of bytes to put
     */
    virtual void put(const void* pData, size_t nBytes);
    /**
     * @brief Puts data from iovecs into the sink (ring)
     * @param iovs Pointer to iovec data
     * @param iovcnt Number of iovecs
     * @throw CErrnoException If the underlying write call fails
     */
    virtual void putV(iovec* iovs, size_t iovcnt);    

private:
    /**
     * @brief Try to open the ring as a producer
     * @throw CErrnoException If a producer already exists for the ring
     * @throw std::runtime_error If the ring otherwise cannot be created
     */
    void openRing();

};
#endif
