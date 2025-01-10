/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2017.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Authors:
             Ron Fox
             Giordano Cerriza
	     Aaron Chester
	     FRIB
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

/** 
 * @file FdDataSource.cpp
 * @brief Implementation of the file descriptor data source.
 */

#include "FdDataSource.h"

#include <RingItemFactoryBase.h>

using namespace ufmt;

/**
 * constructor
 * @param pFactory Pointer to the factory used to get items.
 * @param fd       File descriptor open on the data source.
 *                 The caller owns this - we don't close it on destruction.
 */
FdDataSource::FdDataSource(RingItemFactoryBase* pFactory, int fd) :
    DataSource(pFactory), m_fd(fd)
{}

/**
 * destructor
 */
FdDataSource::~FdDataSource() {}

/**
 * getItem
 * @return Undifferentiated ring item dynamically created. nullptr if there's 
 *         no more.
 */
CRingItem*
FdDataSource::getItem()
{
    return m_pFactory->getRingItem(m_fd);
}
