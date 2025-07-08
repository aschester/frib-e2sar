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
 * @file BufferedSink.h
 * @brief Provides a class for managing a data sink with buffered output.
 */

#ifndef BUFFEREDSINK_H
#define BUFFEREDSINK_H

#include <memory>
#include <string>
#include <deque>
#include <mutex>

namespace boost {
    class thread;
}
class DataSink;


/**
 * @struct Buffer
 * @brief Wrapper for a data buffer and its size. Frees memory associated with 
 * the buffer on destruction.
 */

struct Buffer
{
    uint64_t s_time; //!< Event timestamp used to order data
    void* s_pData;   //!< The data
    size_t s_size;   //!< Size of the data buffer

    Buffer(uint64_t time, void* pData, size_t size)
	: s_time(time), s_pData(pData), s_size(size) {};
    ~Buffer() { free(s_pData); };
};

/**
 * @class BufferedSink
 * @brief A class for managing a DataSink object and buffering data into it.
 */

class BufferedSink
{
private:
    size_t m_timeout; //!< Timeout seconds for outputting data
    uint64_t m_window; //!< Sliding window for outputting data
    uint64_t m_lastEmitted; //!< s_time value of last Buffer emitted
    std::deque<Buffer*> m_evtList; //!< List of events sorted by timestamp
    std::unique_ptr<DataSink> m_pSink; //!< Our data sink
    std::unique_ptr<boost::thread> m_pOutThread; //!< Thread for output
    std::mutex m_mutex; //!< Mutex for locking container access
    
public:
    /** @brief Construct from URI */
    BufferedSink(std::string uri, size_t timeout=2, size_t window=10);
    /** @brief Destructor */
    ~BufferedSink();

    void addData(uint64_t timestamp, void* pData, size_t nBytes);
    void stopThreads();

    void setTimeout(size_t timeout) { m_timeout = timeout; };
    size_t getTimeout() { return m_timeout; };
    void setWindow(size_t window) { m_window = window; };
    size_t getWindow() { return m_window; };

private:
    /**
     * @brief Create the sink from a URI
     * @param uri The sink URI
     * @return Pointer to dynamically created data sink
     * @throw std::runtime_error If the sink protocol is not recognized
     */
    DataSink* makeDataSink(std::string uri);
    
    void insertBuffer(Buffer* pBuffer);
    void poll();
    void outputData();
    uint64_t getFirstTime() {
	return (m_evtList.empty() ? 0 : m_evtList.front()->s_time);
    };
    uint64_t getLastTime() {
	return (m_evtList.empty() ? 0 : m_evtList.back()->s_time);
    };
    uint64_t queueTimeDifference() { return getLastTime() - getFirstTime(); };
    /**
     * @brief Write data to a sink
     * @param pData Data buffer to write
     * @param nBytes Number of bytes in buffer
     * @param pSink Pointer to data sink we're writing to
     */
    void write(void* pData, size_t nBytes);
    /**
     * @brief Return the size of the item
     * @param pData Pointer to a ring item
     * @return Number of bytes in that item
     */
    size_t itemSize(void* pData);
    /**
     * @brief Get pointer to beginning of next item
     * @param pData Pointer to data block containing ring items
     * @return void* Pointer to the next item in the block
     */
    void* nextItem(void* pData);
    /**
     * @brief Count the number of items in a block of data
     * @param pData Pointer to data block containing ring items
     * @param nBytes Number of bytes in the block
     * @return Number of items in the block
     */
    size_t countRingItems(void* pData, size_t nBytes);
};

#endif
