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
    CRingBuffer* m_pRing;
    std::string  m_ringName; 
 
  public:
    RingDataSink(std::string ringName);
    virtual ~RingDataSink();

  private:
    RingDataSink(const RingDataSink& rhs);
    RingDataSink& operator=(const RingDataSink& rhs);
    int operator==(const RingDataSink& rhs) const;
    int operator!=(const RingDataSink& rhs) const;

    // Implementation of required interface:
public:
    virtual void putItem(const ufmt::CRingItem& item);
    virtual void put(const void* pData, size_t nBytes);
    virtual void putV(iovec* iovs, size_t iovcnt);    

private:
    void openRing();

};
#endif
