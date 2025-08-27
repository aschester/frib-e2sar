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
 * @file BufferPool.cpp
 * @brief Implement the lock-free buffer pool.
 */

#include "BufferPool.h"

#include <iostream>

static const int RETRY_ATTEMPTS = 5; //!< Number of `push()` retries

using namespace boost::lockfree;

BufferPool::BufferPool(size_t queueSize, size_t bufferSize) :
    m_queue(queueSize),
    m_pool(bufferSize)
{}

BufferPool::~BufferPool()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    void* buffer;
    while(m_queue.pop(buffer)) {
	m_pool.free(buffer);
    }
    
    m_pool.purge_memory();
}

/**
 * @details
 * Allocating a new buffer from the pool is not threadsafe, so we use a 
 * std::lock_guard to automatically lock that section. It is the responsiblity
 * of the caller to ensure that the returned buffer is the proper type.
 */
void*
BufferPool::pop()
{
    void* buffer;
    if (!m_queue.pop(buffer)) {
	std::lock_guard<std::mutex> lock(m_mutex);
	buffer = m_pool.malloc();
	if (!buffer) {
	    throw std::bad_alloc();
	}
    }

    return buffer;
}

/**
 * @details
 * If the queue is full, try again a few more times. If that fails, free the 
 * memory and warn the user.
 */
void
BufferPool::push(void* pData) {
    for (int i = 0; i < RETRY_ATTEMPTS; i++) {
	if (m_queue.push(pData)) {
	    return;
	}
    }
    std::cerr << "Failed to push buffer to queue, freeing instead..."
	      << std::endl;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.free(pData);
}
