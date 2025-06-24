/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2017.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Authors:
             Aaron Chester
             FRIB
             Michigan State University
             East Lansing, MI 48824-1321
*/

/**
 * @file BufferPool.h
 * @brief A buffer pool for recycling buffers using a lock-free queue.
 */

#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include <memory>
#include <mutex>

#include <boost/lockfree/lockfree_forward.hpp>
#include <boost/pool/poolfwd.hpp>

/**
 * @class BufferPool
 * @brief A buffer pool implemented using a lock-free queue.
 * @details
 * This is a memory-management class for E2SAR workflows when recycling data 
 * buffers is needed. The class manages a Boost lock-free queue to store 
 * recycled buffers and uses a Boost pool to allocate fixed-sized blocks of 
 * memory. The size of the queue (max number of elements) and the size of each 
 * data block are determined on construction.
 */

namespace boost {
    template<typename UserAllocator> class pool;
}

class BufferPool
{
private:
    std::unique_ptr<boost::lockfree::queue<void*, boost::lockfree::fixed_sized<true>>> m_pQueue; //!< Queue for storing recycled buffers
    std::unique_ptr<boost::pool<>> m_pPool; //!< Manages storage for the class
    std::mutex m_mutex; //!< Mutex for locking the pool
    
public:
    /**
     * @brief Construct with a fixed size
     * @param queueSize Elements in the queue
     * @param bufferSize Size of each queue element
     */
    BufferPool(size_t queueSize, size_t bufferSize);
    /** @brief Destructor */
    ~BufferPool();

    /**
     * @brief Pop a buffer from the queue
     * @return Pointer to data buffer
     * @throw std::bad_alloc If new buffer memory cannot be allocated
     */
    void* pop();
    /**
     * @brief Push data onto the queue
     * @param pData Pointer to data buffer
     */
    void push(void* pData);
    /** @brief Pop all data off the queue and free associated memory */
    void free();
};

#endif
