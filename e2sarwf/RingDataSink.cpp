/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2015.

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
 * @file RingDataSink.cpp
 * @brief Implement the ringbuffer sink
 */

#include "RingDataSink.h"

#include <iostream>
#include <stdexcept>
#include <sys/uio.h>

#include <DataFormat.h>
#include <CRingItem.h>

#include <CRingBuffer.h>

using namespace ufmt;

RingDataSink::RingDataSink(std::string ringName) :
    m_pRing(nullptr), m_ringName(ringName)
{
    openRing();
}

RingDataSink::~RingDataSink()
{
    delete m_pRing;
    m_pRing = nullptr;
}

void RingDataSink::putItem(const CRingItem& item)
{
    put(item.getItemPointer(), item.size());
}

void RingDataSink::put(const void* pData, size_t nBytes)
{
    /**
     * @todo This is the theoretical correct way to do this as there is a 
     * (very long) timeout on the put if none is specified (years)... should 
     * at some point check that all 'reliable' puts are done like this -- 
     * retrying until success.
     */
    while(!m_pRing->put(pData, nBytes));
}

/**
 * @details
 * The implementation here is pretty naive: we just loop over the iovs and 
 * call `put()` on each one.
 */
void RingDataSink::putV(iovec* iovs, size_t iovcnt)
{
    for (size_t i = 0; i < iovcnt; i++) {
	put(iovs[i].iov_base, iovs[i].iov_len);
    }
}

/**
 * @details
 * Here we:
 * - Check if the ring exists
 * - If so, attach as producer
 * - If not, create it and attach as a producer
 */
void RingDataSink::openRing()
{
    if (CRingBuffer::isRing(m_ringName)) {
	m_pRing = new CRingBuffer(m_ringName,CRingBuffer::producer);
    } else {
	//size_t size = 128*1024*1024;
	m_pRing = CRingBuffer::createAndProduce(m_ringName);
    }

    if (!m_pRing) {
	std::string msg("RingDataSink::openRing() failed to create ringbuffer");
	throw std::runtime_error(msg);
    }
}
