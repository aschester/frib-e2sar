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
 * @file  DataSource.h
 * @brief Provide a data source for undifferentiated ring items.
 */

#ifndef DATASOURCE_H
#define DATASOURCE_H

namespace ufmt {
    class CRingItem;
    class RingItemFactoryBase;
}

/**
 * @class DataSource
 * @brief Abstract data source base class
 * @details
 * Pure abstract data source to provide ring items from a data source using a 
 * ring item factory. We'll also need some concrete classes:
 * - FdDataSource: give data from a file descriptor.
 * - RingDataSource: give data from a ringbuffer.
 * - StreamDataSource: give data from a stream.
 * @note This requires a version of the ufmt library which is compiled against 
 * NSCLDAQ - we incorp ufmt as part of the build and build it against the 
 * build's NSCLDAQ_ROOT.
 */

class DataSource {
protected:
    ufmt::RingItemFactoryBase* m_pFactory; //!< Ptr to our ring item factory.
    
public:
    /** 
     * @brief Constructor
     * @param pFactory Pointer to concrete ring item factory
     */
    DataSource(ufmt::RingItemFactoryBase* pFactory);
    /** @brief Destructor */
    virtual ~DataSource() = default;
    /** 
     * @brief Pure-virtual method to access a ring item from the data source 
     * Must be implemented in derived classes
     * @return Pointer to the next ring item from the source
     */
    virtual ufmt::CRingItem* getItem() = 0;
    /**
     * @brief Set a new factory 
     * @param pFactory Pointer to new ring item factory
     */
    void setFactory(ufmt::RingItemFactoryBase* pFactory);
};


#endif
