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

#include <mutex>

#include <boost/lockfree/queue.hpp>
#include <boost/pool/pool.hpp>

/**
 * @class BufferPool
 * @brief A buffer pool implemented using a lock-free queue.
 * @details
 * This is a memory-management class for E2SAR workflows when recycling data 
 * buffers is needed. The class manages a Boost lock-free queue to store 
 * recycled buffers and uses a Boost pool to allocate fixed-sized blocks of 
 * memory. The size of the queue (max number of elements) and the size of each 
 * data block are determined on construction.
 *
 * A lock guard to ensures critical parts of the code are not run concurrently 
 * by multiple threads. An alternative is to use Boost singleton pool, but 
 * that requires the buffer size to be known at compile time. This approach 
 * gives us a little more flexibility at the (possible) cost of some simplicity
 * managing thread safety with a singleton. There are no auto-generated copy or
 * move constructors or assignment operators - don't try with this class!
 */

namespace boost {
    template<typename UserAllocator> class pool;
}

class BufferPool
{
private:
    boost::lockfree::queue<void*, boost::lockfree::fixed_sized<true>> m_queue; //!< Queue for storing recycled buffers
    boost::pool<> m_pool; //!< Manages storage for the class
    std::mutex m_mutex; //!< Mutex for locking pool
    
public:
    /**
     * @brief Construct with a fixed size
     * @param queueSize Elements in the queue
     * @param bufferSize Size of each queue element
     */
    BufferPool(size_t queueSize, size_t bufferSize);
    /** @brief Destructor */
    ~BufferPool();
    /** @brief Copy constructor (deleted) */
    BufferPool(const BufferPool&) = delete;
    /** @brief Copy assigment (deleted) */
    BufferPool& operator=(const BufferPool&) = delete;
    /** @brief Move constructor (deleted) */
    BufferPool(BufferPool&&) = delete;
    /** @brief Move assigment (deleted) */
    BufferPool& operator=(BufferPool&&) = delete;

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
};

#endif
