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
#include <thread>
#include <vector>

#include <boost/chrono.hpp>

#include <DataFormat.h> // From UFMT

#include <URL.h>        // From NSCLDAQ

#include "DataSink.h"
#include "FileDataSink.h"
#include "RingDataSink.h"

std::atomic<size_t> approxInputSize(0); //!< Appoximate size of input queue

using namespace ufmt;
namespace ch = std::chrono;

/**
 * @details
 * Create the sink from the passed URI. It is up to the caller to ensure that 
 * the URI string is well formed. Starts threads.
 */
BufferedSink::BufferedSink(std::string uri, size_t queueSize, bool useTs,
			   size_t timeout, size_t window) :
    m_timeout(timeout),
    m_window(window),
    m_lastEmitted(0),
    m_shutdown(false),
    m_inputQueueCapacity(queueSize),
    m_inputFifo(queueSize/8),
    m_inputQueue(queueSize)
{
    if (useTs) {
	window *= 1e9;
    }
    std::cerr << "Window size: " << m_window
	      << (useTs ? " nanoseconds" : " events")
	      << std::endl;
    
    m_pSink = std::unique_ptr<DataSink>(makeDataSink(uri));

    m_sortThread = boost::thread(&BufferedSink::pollInputQueue, this);
    m_outputThread = boost::thread(&BufferedSink::outputData, this);
}

BufferedSink::~BufferedSink()
{
    auto duration = ch::milliseconds(100);
    
    stopThreads();

    std::this_thread::sleep_for(duration); // Give a bit of time to stop

    // Hopefully cleaned up when the threads stop but:
    
    Buffer* pBuffer;
    while (m_inputQueue.pop(pBuffer)) {
	delete pBuffer;
    }
    
    for (auto p : m_sortedQueue) {
	delete p;
    }
    
    m_sortedQueue.clear();
    m_outputQueue.clear();
}

void
BufferedSink::stopThreads()
{
    auto duration = ch::milliseconds(1000);

    m_shutdown.store(true);
    
    m_inputReady.notify_all();    
    m_outputReady.notify_all();

    std::this_thread::sleep_for(duration); // Give a bit of time to stop

    if (m_sortThread.joinable()) {
        m_sortThread.join();
    }

    std::this_thread::sleep_for(duration); // Give a bit of time to stop
    
    if (m_outputThread.joinable()) {
        m_outputThread.join();
    }

    std::this_thread::sleep_for(duration); // Give a bit of time to stop
    
    std::cout << "Flushing remaining event buffer data..." << std::endl;

    while (!m_inputQueue.empty()) {
	std::cout << "Draining input queue..." << std::endl;
	drainInputQueue();
    }

    while (!m_sortedQueue.empty()) {
	std::cout << "Preparing sorted data for output..." << std::endl;
	prepareDataForOutput();
    }

    while (!m_outputQueue.empty()) {
	std::cout << "Final drain of output queue..." << std::endl;
	writeToSink();
    }
}

/**
 * @details
 * The Buffer new'd here is deleted by the output thread.
 */
void
BufferedSink::addData(uint64_t timestamp, void* pData, size_t nBytes)
{
    auto pBuffer = new Buffer(timestamp, pData, nBytes);
    
    if (!m_inputQueue.push(pBuffer)) {
	std::cerr << "**WARNING** Input queue full, dropping data" << std::endl;
	delete pBuffer;
	return;
    } else {
	approxInputSize++;
	// if (approxInputSize%100==0)
	//     std::cerr << "approxInputSize=" << approxInputSize << std::endl;
    }

    if (approxInputSize > m_inputFifo) {
	m_inputReady.notify_one();
    }
}

void
BufferedSink::setInputQueueFifo(size_t threshold)
{
    if (threshold > m_inputQueueCapacity) {
	std::cerr << "FIFO depth=" << threshold << " exceeds queue capacity="
		  << m_inputQueueCapacity << " increase the queue "
		  << "capacity or choose a smaller FIFO threshold"
		  << std::endl;
	std::cerr << "**WARNING** FIFO threshold value unchanged" << std::endl;
	return;
    }
    m_inputFifo = threshold;
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
 * We know that our insertion pattern should be at or "near" the back "almost" 
 * always, so we'll use back inseriton rathern than something like binary 
 * search for the insertion point. Assumes the caller holds whatever lock(s) 
 * are needed.
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

void
BufferedSink::pollInputQueue()
{
    auto lastOutputTime = ch::high_resolution_clock::now();
    
    while (!m_shutdown.load()) {
	{
	    // std::cerr << "[pollInputQueue] waiting for input" << std::endl;
	    // std::cerr << "[pollInputQueue] acquiring input mutex" << std::endl;
	    std::unique_lock<std::mutex> lock(m_inputMutex);
	    // std::cerr << "[pollInputQueue] input mutex acquired" << std::endl;
	    m_inputReady.wait_for(lock, ch::milliseconds(100), [this] {
		return m_shutdown.load();
	    });
	    // std::cerr << "[pollInputQueue] wait complete" << std::endl;

	    // std::cerr << "[poll] atomic_input_size=" << approxInputSize << std::endl;
	    drainInputQueue();
	    
	    // We are ready to output data if one of two things are true:
	    // 1. We have hit a timeout limit
	    // 2. The time difference between the first and last item in the
	    //    queue exceeds the emission window

	    auto now = ch::high_resolution_clock::now();
	    bool timeout = (now - lastOutputTime) > ch::seconds(m_timeout);
	    bool window = readyEmitFromWindow();
	    bool ready = timeout || window;
	    
	    if (ready) {
		// auto diff = now - lastOutputTime;
		// auto d = ch::duration_cast<ch::milliseconds>(diff);
		// std::cerr << "[poll] triggered for output" << std::endl;
		// std::cerr << "[poll] sorted_queue_size=" << m_sortedQueue.size()
		// 	  << " write_queue_size=" << m_outputQueue.size() 
		// 	  << " timeout=" << timeout 
		// 	  << " window_ready=" << window
		// 	  << " time_since_last=" << d.count() << "ms"
		// 	  << std::endl;
		
		m_outputReady.notify_one();
		lastOutputTime = ch::high_resolution_clock::now();
	    }
	    // std::cerr << "[pollInputQueue] releasing input mutex" << std::endl;
	} // Release lock
	// std::cerr << "[pollInputQueue] input mutex released" << std::endl;
    } // End poll loop
}

/**
 * @details
 * Caller has the sorted queue lock
 */
void
BufferedSink::drainInputQueue()
{
    /**
     * @note (ASC 9/10/25): Too-frequent allocations here? Can be member.
     */ 
    std::vector<Buffer*> batch; // This guy as a member variable
    size_t maxDrain = m_inputQueueCapacity + 100;
    batch.reserve(maxDrain);    // Reserve in e.g., constructor
    
    Buffer* pBuffer;
    while (batch.size() < maxDrain && m_inputQueue.pop(pBuffer)) {
	batch.push_back(pBuffer);
	approxInputSize--;
	// if (approxInputSize%100==0)
	//     std::cerr << "approxInputSize=" << approxInputSize << std::endl;
    }

    if (!batch.empty()) {
	{
	    // std::cerr << "[drainInputQueue] batch size " << batch.size()
	    // 	      << std::endl;
	    // std::cerr << "[drainInputQueue] acquiring sort mutex" << std::endl;
	    std::unique_lock<std::mutex> lock(m_sortMutex);
	    // std::cerr << "[drainInputQueue] sort mutex acquired" << std::endl;
	    for (auto b : batch) {
		insertBuffer(b);
	    }
	    // std::cerr << "[drainInputQueue] releasing mutex released"
	    // 	      << std::endl;
	} // Release sort mutex
	// std::cerr << "[drainInputQueue] sort mutex released" << std::endl;
    }
}


/**
 * @details
 * Thread safety requires that only the prepare thread call this function as 
 * concurrent access invalidates checks on the queue times.
 */
bool
BufferedSink::readyEmitFromWindow()
{
    std::unique_lock<std::mutex> lock(m_sortMutex);
    
    // At least two elements needed to check sliding window:
    
    if (m_sortedQueue.size() < 2) {
	return false;
    }
    
    auto tdiff = m_sortedQueue.back()->s_time - m_sortedQueue.front()->s_time;
    
    return (tdiff > m_window);
}

void
BufferedSink::outputData()
{
    while (!m_shutdown.load()) {
	{
	    // std::cerr << "[outputData] waiting for input" << std::endl;
	    // std::cerr << "[outputData] acquiring output mutex" << std::endl;
	    std::unique_lock<std::mutex> lock(m_outputMutex);
	    // std::cerr << "[outputData] output mutex acquired" << std::endl;
	    m_outputReady.wait_for(lock, ch::seconds(m_timeout), [this] {
		return m_shutdown.load();
	    });
	    // std::cerr << "[outputData] wait complete" << std::endl;

	    // auto t0 = ch::high_resolution_clock::now();
	    prepareDataForOutput();
	    // auto t1 = ch::high_resolution_clock::now();
	    // auto d = ch::duration_cast<ch::milliseconds>(t1 - t0);
	    // std::cerr << "[outputData] prepare timer=" << d.count() << "ms" << std::endl;
	    // t0 = ch::high_resolution_clock::now();
	    writeToSink();
	    // t1 = ch::high_resolution_clock::now();
	    // d = ch::duration_cast<ch::milliseconds>(t1 - t0);
	    // std::cerr << "[outputData] write timer=" << d.count() << "ms" << std::endl;
	    // std::cerr << "[outputData] releasing output mutex" << std::endl;
	} // Release lock mutex
	// std::cerr << "[outputData] output mutex released" << std::endl;        
    }
}

/**
 * @details
 * Caller holds the output lock
 */
void
BufferedSink::prepareDataForOutput()
{
    std::deque<Buffer*> ready;
    {
	// std::cerr << "[prepareDataForOutput] waiting for sort" << std::endl;
	// std::cerr << "[prepareDataForOutput] acquiring sort mutex" << std::endl;
	std::unique_lock<std::mutex> lock(m_sortMutex);
	// std::cerr << "[prepareDataForOutput] sort mutex acquired" << std::endl;

	if (m_sortedQueue.empty()) {
	    // std::cerr << "**WARNING** Trying to output data but the "
	    // 	      << "sorted queue is empty" << std::endl;
	    return;
	}

	// Queue buffers for outputting:

	auto outputUntil = m_sortedQueue.front()->s_time + m_window;
	// std::cerr << "[prepareDataForOutput] window limit " << outputUntil
	// 	  << std::endl;
	while (!m_sortedQueue.empty()
	       && m_sortedQueue.front()->s_time < outputUntil) {
	    ready.push_back(m_sortedQueue.front());
	    m_sortedQueue.pop_front();
	}

	// std::cerr << "[prepareDataForOutput] releasing sort mutex" << std::endl;
    } // Release sort mutex
    // std::cerr << "[prepareDataForOutput] sort mutex released" << std::endl;

    if (ready.empty()) {
	std::cerr << "**WARNING** No data ready for output" << std::endl;
	return;
    }
    
    if (!m_outputQueue.empty()) {
	std::cerr << "**WARNING** Trying to populate output queue but it "
		  << "already has contains data" << std::endl;
    }

    for (auto b : ready) {
	m_outputQueue.emplace_back(b);
    }    
}

/**
 * @details
 * Caller holds the output lock
 */
void
BufferedSink::writeToSink()
{
    if (m_outputQueue.empty()) {
        // std::cerr << "[writeToSink] Output queue is empty, nothing to write"
		  // << std::endl;
        return;
    }

    auto last = m_outputQueue.back()->s_time;
    
    while (!m_outputQueue.empty()) {
        const auto& b = m_outputQueue.front();
        m_pSink->put(b->s_pData, b->s_size);
        m_outputQueue.pop_front(); // Pop the item after it's processed
    }

    if (last < m_lastEmitted) {
	std::cerr << "**WARNING** buffer time moving backwards from " <<
	    m_lastEmitted << " to " << last << std::endl;
    }
    m_lastEmitted = last;
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
