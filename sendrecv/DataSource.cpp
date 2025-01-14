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
 * @file DataSource.cpp
 * @brief Implementation of the non pure vir. methods of DataSource.
 */

#include "DataSource.h"

#include <RingItemFactoryBase.h>

using namespace ufmt;

/**
 * Constructor
 * @brief Just saves the factory pointer - note that we gain ownership of the 
 * factory and, therefore, it's deleted on our destruction.
 */
DataSource::DataSource(RingItemFactoryBase* pFactory) :
    m_pFactory(pFactory)
{}

/**
 * destructor
 * @brief Destroys the factory.
 */
DataSource::~DataSource()
{
    delete m_pFactory;
}

/**
 * setFactory
 * @brief Set a new factory
 * @details
 *   - delete the current factory
 *   - set a new factory - this is done if the format changes.
 * @param pFactory New factory to set.
 */
void
DataSource::setFactory(RingItemFactoryBase* pFactory)
{
    delete m_pFactory;
    m_pFactory = pFactory;
}
