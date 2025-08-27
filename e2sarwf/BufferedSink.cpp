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
 * @file BufferedSink.cpp
 * @brief Implement the buffered data sink.
 */

#include "BufferedSink.h"

#include <chrono>
#include <iostream>
#include <vector>
#include <sys/uio.h>


#include <DataFormat.h> // From UFMT

#include <URL.h>        // From NSCLDAQ

#include "DataSink.h"
#include "FileDataSink.h"
#include "RingDataSink.h"

const size_t BATCH_SIZE = 2048; //!< Max buffers per batch

using namespace ufmt;
namespace ch = std::chrono;

/**
 * @details
 * Create the sink from the passed URI. It is up to the caller to ensure that 
 * the URI string is well formed. Starts output thread.
 */
BufferedSink::BufferedSink(std::string uri, size_t queueSize, size_t timeout, size_t window) :
    m_timeout(timeout),
    m_window(window*1e9),
    m_lastEmitted(0),
    m_inputQueue(queueSize),
    m_sortedQueue(),
    m_pSink(std::unique_ptr<DataSink>(makeDataSink(uri))),
    m_outThread(&BufferedSink::poll, this),
    m_shutdown(false)    
{}

BufferedSink::~BufferedSink()
{
    stopThreads();

    Buffer* pBuffer;
    while (m_inputQueue.pop(pBuffer)) {
	delete pBuffer;
    }
    // No clear for boost::lockfree::queue
    
    for (auto p : m_sortedQueue) {
	delete p;
    }
    m_sortedQueue.clear();
}

void
BufferedSink::addData(uint64_t timestamp, void* pData, size_t nBytes)
{
    auto pBuffer = new Buffer(timestamp, pData, nBytes);
    
    if (!m_inputQueue.push(pBuffer)) {
	std::cerr << "**WARNING** Input queue full, dropping data" << std::endl;
	delete pBuffer;
	return;
    }

    m_dataReady.notify_one();
}

void
BufferedSink::stopThreads()
{
    m_shutdown.store(true);
    m_dataReady.notify_all(); // Wake up output thread

    if (m_outThread.joinable()) {
	m_outThread.join();
    }
    
    std::cout << "Flushing remaining event buffer data..." << std::endl;
    drainInputQueue();
    outputData();
}

/****************************************************************************
 * Private functions                                                        *
 ***************************************************************************/

/**
 * @details
 * Convert the URI string to an NSCLDAQ URL. Will throw if the URI string is 
 * malformed, either due to failed URL construction or an unrecognized protocol.
 */
DataSink*
BufferedSink::makeDataSink(std::string uri)
{
    URL url(uri);
    std::string proto(url.getProto());
    std::string path(url.getPath());
    if (proto == "tcp" || proto == "ring") {
	return new RingDataSink(path);
    } else if (proto == "file") {
	return new FileDataSink(path);
    } else {
	throw std::runtime_error("Unknown protocol for sink " + proto);
    }
}

/**
 * @details
 * Assumes the caller holds the lock
 */
void
BufferedSink::insertBuffer(Buffer* pBuffer)
{    
    if (pBuffer->s_time < m_lastEmitted) {
	std::cerr << "**WARNING** Data late: current " << pBuffer->s_time
		  << " last emitted " << m_lastEmitted << std::endl;
    }
    
    // Can just push when list is empty or time >= last time:
    
    if (m_sortedQueue.empty()
	|| (pBuffer->s_time >= m_sortedQueue.back()->s_time)) {
	m_sortedQueue.push_back(pBuffer);
	return;
    }

    // If time < earliest, put in the front:

    if (pBuffer->s_time < m_sortedQueue.front()->s_time) {	
	m_sortedQueue.push_front(pBuffer);
	return;
    }

    // Otherwise, search for an insertion point starting at the back:

    auto rit = m_sortedQueue.rbegin();
    while (rit != m_sortedQueue.rend() && (*rit)->s_time > pBuffer->s_time) {
        ++rit;
    }
    auto it = rit.base(); // Forward iterator
    m_sortedQueue.insert(it, pBuffer); // Insert before iterator position
}

/**
 * @details
 * This function is run by the output thread. Contents of the sorted queue are 
 * modified only by the this thread. Be mindful of this if you are trying to 
 * modify this class! 
 */
void
BufferedSink::poll()
{
    auto lastOutputTime = ch::high_resolution_clock::now();
    
    while (!m_shutdown.load()) {

	// Move data from the input to the sorting queue:
	
	drainInputQueue();

	// We are ready to output data if one of two things are true:
	// 1. We have hit a timeout limit
	// 2. The time difference between the first and last item in the
	//    queue exceeds the emission window
	    
	auto now = ch::high_resolution_clock::now();
	bool timeout = (now - lastOutputTime) > ch::seconds(m_timeout);
	bool ready = (timeout && !m_sortedQueue.empty()); // 1. timeout
	ready |= readyEmitFromWindow();                   // 2. window

	// If we're ready to output data, do so, else sleep and wait:
	    
	if (ready) {
	    outputData();
	    lastOutputTime = now;
	} else {
	    std::unique_lock<std::mutex> lock(m_mutex);
	    m_dataReady.wait_for(lock, ch::milliseconds(100));
	}	    
    }    
}

/**
 * @details
 * Output thread moves data from the input to the sorted queue in batches with 
 * a max batch size defined by BATCH_SIZE. Contents of the sorted queue should
 * only be modified by the output thread, though there is no real mechanism
 * to ensure that this is the case. Be careful!
 */
void
BufferedSink::drainInputQueue()
{
    std::vector<Buffer*> batch;
    batch.reserve(BATCH_SIZE);

    Buffer* pBuffer;
    while (batch.size() < BATCH_SIZE && m_inputQueue.pop(pBuffer)) {
	batch.push_back(pBuffer);
    }

    if (!batch.empty()) {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto b : batch) {
	    insertBuffer(b);
	}
    }
}

/**
 * @details
 * Thread safety requires that only the output thread call this function as 
 * concurrent access invalidates checks on the queue times.
 * @note (ASC 8/27/25): A safer pattern is to cordon off the output thread 
 * into its own class...
 */
bool
BufferedSink::readyEmitFromWindow()
{
    // At least two elements needed to check sliding window:
    
    if (m_sortedQueue.size() < 2) {
	return false;
    }

    auto tdiff = m_sortedQueue.back()->s_time - m_sortedQueue.front()->s_time;

    return tdiff > m_window;
}

/**
 * @details
 * For now we stick to the simplest approach - output everything until the 
 * sliding window limit. 
 * @note (ASC 8/27/25): Another interesting approach to try is to force use of 
 * the event counter as an event number and output consecutive events - this 
 * allows us to check if data is missing within the sliding window and 
 * terminate the output prematurely to allow more data to come in.
 */
void
BufferedSink::outputData()
{
    // Nothing to output, this should be impossible if we've gotten here:
    
    if (m_sortedQueue.empty()) {
	std::cerr << "**WARNING** Trying to output data but the "
		  << "sorted queue is empty " << std::endl;
  	return;
    }

    // Queue buffers for outputting:
    
    auto outputUntil = m_sortedQueue.front()->s_time + m_window;
    std::deque<std::unique_ptr<Buffer>> outputBuffers;
    size_t totalItems = 0;
    while (!m_sortedQueue.empty()
	   && m_sortedQueue.front()->s_time <= outputUntil) {
	auto pBuffer = std::unique_ptr<Buffer>(m_sortedQueue.front());
	totalItems += countRingItems(pBuffer->s_pData, pBuffer->s_size);
	outputBuffers.push_back(std::move(pBuffer));
	m_sortedQueue.pop_front();
    }

    // Reset last time:

    m_lastEmitted = outputBuffers.back()->s_time;
    
    // Consolidate all buffers into a single iovec and make a "single" write
    // call, relying on the underlying writes to bust up the iovec into
    // appropriate chunks if its too large:

    std::vector<iovec> iovs;
    iovs.reserve(totalItems);
    
    for (const auto& b : outputBuffers) {
	auto pData = static_cast<u_int8_t*>(b->s_pData);
	size_t nItems = countRingItems(pData, b->s_size);
	for (size_t i = 0; i < nItems; i++) {
	    iovs.emplace_back(iovec{pData, itemSize(pData)});	    
	    pData = static_cast<u_int8_t*>(nextItem(pData));
	}
    }
    
    m_pSink->putV(iovs.data(), iovs.size());
}

// Ring item utilities:

size_t
BufferedSink::itemSize(void* pData)
{
    return static_cast<RingItemHeader*>(pData)->s_size;
}

void*
BufferedSink::nextItem(void* pData)
{
    size_t n = itemSize(pData);
    uint8_t* p = static_cast<uint8_t*>(pData);
    p += n;
    
    return p;
}

size_t
BufferedSink::countRingItems(void* pData, size_t nBytes)
{
    size_t result = 0;
    while (nBytes) {
        result++;
        nBytes -= itemSize(pData);
        pData   = nextItem(pData);
    }
    
    return result;
}
