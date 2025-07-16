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

#include <iostream>
#include <vector>
#include <sys/uio.h>

#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include <DataFormat.h> // From UFMT

#include <URL.h>        // From NSCLDAQ

#include "DataSink.h"
#include "FileDataSink.h"
#include "RingDataSink.h"

using namespace ufmt;
namespace ch = boost::chrono;

/**
 * @details
 * Create the sink from the passed URI. It is up to the caller to ensure that 
 * the URI string is well formed.
 */
BufferedSink::BufferedSink(std::string uri, size_t timeout, size_t window) :
    m_timeout(timeout),
    m_window(window*1e9),
    m_lastEmitted(0),
    m_evtList(),
    m_pSink(std::unique_ptr<DataSink>(makeDataSink(uri))),
    m_pOutThread(std::make_unique<boost::thread>(&BufferedSink::poll, this))
{}

BufferedSink::~BufferedSink()
{
    m_pOutThread->interrupt();
    m_pOutThread->join();

    for (auto p : m_evtList) {
	delete p;
    }
    m_evtList.clear();
}

void
BufferedSink::addData(uint64_t timestamp, void* pData, size_t nBytes)
{
    auto pBuffer = new Buffer(timestamp, pData, nBytes);
    insertBuffer(pBuffer);
}

void
BufferedSink::stopThreads()
{
    m_pOutThread->interrupt();
    m_pOutThread->join();
    std::cout << "Flushing remaining event buffer data..." << std::endl;
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

void
BufferedSink::insertBuffer(Buffer* pBuffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (pBuffer->s_time < m_lastEmitted) {
	std::cerr << "**WARNING** Data late: current " << pBuffer->s_time
		  << " last emitted " << m_lastEmitted << std::endl;
    }
    
    // Can just push when list is empty or time >= last time:
    
    if (m_evtList.empty() || (pBuffer->s_time >= m_evtList.back()->s_time)) {
	m_evtList.push_back(pBuffer);
	return;
    }

    // If time < earliest, put in the front:

    if (pBuffer->s_time < m_evtList.front()->s_time) {	
	m_evtList.push_front(pBuffer);
	return;
    }

    // Otherwise, search for an insertion point starting at the back:

    auto it = --m_evtList.end(); // Iterator to last element in the list
    while ((*it)->s_time > pBuffer->s_time) {	
	--it;
    }
    
    m_evtList.insert(++it, pBuffer);
}

void
BufferedSink::poll()
{
    try {
	auto start = ch::high_resolution_clock::now();
	while (true) {
	    boost::this_thread::interruption_point();
	    auto now = ch::high_resolution_clock::now();
	    if (queueTimeDifference() > m_window) {
		outputData();
		start = now;
	    } else if (now - start > ch::seconds(m_timeout)) {
		outputData();
		start = now;
	    }
	}
    }
    catch (const boost::thread_interrupted& e) {
	std::cout << "Interrupted output thread "
		  << boost::this_thread::get_id()
		  << std::endl;
    }
}

void
BufferedSink::outputData()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_lastEmitted = getLastTime();
    
    while (!m_evtList.empty() && m_evtList.front()->s_time <= m_lastEmitted) {
	std::unique_ptr<Buffer> pBuffer(m_evtList.front());
	m_evtList.pop_front();
	write(pBuffer->s_pData, pBuffer->s_size);
    }   
}

/**
 * @details
 * Creates iovecs of data and calls the sink's `putV()` method to do the 
 * actual write.
 */
void
BufferedSink::write(void* pData, size_t nBytes)
{
    auto p = static_cast<u_int8_t*>(pData);
    size_t nItems = countRingItems(p, nBytes);
    std::vector<iovec> iovs(nItems);
    
    for (size_t i = 0; i < nItems; i++) {
	iovs[i].iov_base = p;
	iovs[i].iov_len = itemSize(p);
	p = static_cast<u_int8_t*>(nextItem(p));
    }

    m_pSink->putV(iovs.data(), iovs.size());
}

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
