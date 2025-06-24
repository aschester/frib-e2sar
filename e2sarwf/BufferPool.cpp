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

#include <boost/lockfree/queue.hpp>
#include <boost/pool/pool.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>

using namespace boost::lockfree;

BufferPool::BufferPool(size_t queueSize, size_t bufferSize) :
    m_pQueue(std::make_unique<queue<void*, fixed_sized<true>>>(queueSize)),
    m_pPool(std::make_unique<boost::pool<>>(bufferSize))
{}

BufferPool::~BufferPool()
{
    free();
    m_pPool->purge_memory();
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
    if (!m_pQueue->pop(buffer)) {
	// Pool isn't threadsafe, so lock while we do the malloc:
	std::lock_guard<std::mutex> lock(m_mutex);
	buffer = m_pPool->malloc();
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
    for (int i = 0; i < 5; i++) {
	if (m_pQueue->push(pData)) {
	    return;
	}
    }
    std::cerr << "Failed to push buffer to queue" << std::endl;
    m_pPool->free(pData);
}

void
BufferPool::free()
{
    void* buffer;
    while(m_pQueue->pop(buffer)) {
	m_pPool->free(buffer);
    }
}
