/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2015.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Jeromy Tompkins 
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

#include "RingDataSink.h"

#include <iostream>
#include <stdexcept>
#include <sys/uio.h>

#include <DataFormat.h>
#include <CRingBuffer.h>
#include <CRingItem.h>

using namespace ufmt;

RingDataSink::RingDataSink(std::string ringName)
  : m_pRing(nullptr),
    m_ringName(ringName)
{
  openRing();
}

RingDataSink::~RingDataSink()
{
    delete m_pRing;
    m_pRing=nullptr;
}

/**
 * putItem
 *    Puts a ring item in the sink.
 * @param item - Reference to the item top ut.
 */
void RingDataSink::putItem(const CRingItem& item)
{
  put(item.getItemPointer(), item.size());

}
/**
 * put
 *   Puts arbitrary data to the sink (ring).
 *
 *   @param pData - Pointer to the buffer holding the data.
 *   @param nBytes - Number of bytes to put.
 */
void RingDataSink::put(const void* pData, size_t nBytes)
{
    
    // TODO:  This is the theoretical correct way to do this as
    //        there is a (very long) timeout on the put if none is
    //        specified (years)..should at some point check that all
    //        'reliable' puts are done like this-- retrying until success.
    //
    while(!m_pRing->put(pData, nBytes));
}

void RingDataSink::putV(iovec* iovs, size_t iovcnt)
{
    // Naively we just loop over the iovs and call `put()` on each...
    for (size_t i = 0; i < iovcnt; i++) {
	put(iovs[i].iov_base, iovs[i].iov_len);
    }
}

void RingDataSink::openRing()
{
  // try to open the ring as a producer...
  // check if the ring exists... if it does just
  // try to attach to it as a producer. If a producer
  // exists, this will throw a CErrnoException of errno=EACCES
  if (CRingBuffer::isRing(m_ringName)) {
    m_pRing = new CRingBuffer(m_ringName,CRingBuffer::producer);
  } else {
    // If we are here, no ring exists already with the desired
    // name so we can create and produce
    m_pRing = CRingBuffer::createAndProduce(m_ringName);
  }
  if (m_pRing==0) {
      /** @todo (ASC 2/27/25): Port over exception class from NSCLDAQ/io. */
      std::string msg("RingDataSink::openRing() failed to create ringbuffer");
      throw std::runtime_error(msg);
  }
}
