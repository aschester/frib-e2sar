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
 * @file RingDataSource.cpp
 * @brief Implement RingDataSource class
 */

#include "RingDataSource.h"

#include <RingItemFactoryBase.h>

using namespace ufmt;

RingDataSource::RingDataSource(RingItemFactoryBase* pFact, CRingBuffer& ring) :
    DataSource(pFact), m_ring(ring)
{}

CRingItem*
RingDataSource::getItem()
{
    return m_pFactory->getRingItem(m_ring);
}

CRingItem*
RingDataSource::getItem(unsigned long timeout)
{
    return m_pFactory->getRingItem(m_ring, timeout);
}
