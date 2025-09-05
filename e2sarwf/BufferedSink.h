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

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <boost/lockfree/queue.hpp>
#include <boost/thread.hpp>

class DataSink;

/**
 * @struct Buffer
 * @brief Wrapper for a data buffer and its size. Frees memory associated with 
 * the buffer on destruction.
 *
 * @note (ASC 8/26/25): I _think_ we're OK to free memory buffers here, but 
 * I need to better understand why - who originally allocates this memory,
 * and when does ownership get transferred here (as it appears to), if
 * indeed it does. Not freeing on destruction makes program memory usage grow
 * without bounds.
 */

struct Buffer
{
    uint64_t s_time; //!< Event timestamp used to order data (ns or evt count)
    void* s_pData;   //!< The data
    size_t s_size;   //!< Size of the data buffer

    /** 
     * @brief Constructor
     * @param time Timestamp (time or event number) used for sorting
     * @param pData Pointer to buffer data
     * @param size Buffer size in bytes
     */
    Buffer(uint64_t time, void* pData, size_t size)
	: s_time(time), s_pData(pData), s_size(size) {};
    /** 
     * @brief Destructor 
     * @details
     * Free memory pointed to by pData 
     */
    ~Buffer() { free(s_pData); };
};

/**
 * @class BufferedSink
 * @brief A class for managing a DataSink object and buffering data into it.
 * This uses a multiple-producer, single-consumer model where multiple threads 
 * may be adding data to the input queue but only the output thread can access 
 * the sorted queue and write data to the sink.
 *
 * @note (ASC 8/27/25): Add an additional constructor parameter to configure 
 * the window for ns timestamps or event counter and set the sliding window 
 * accordingly. Allows for dynamic selection of event number convention.
 */

class BufferedSink
{
private:
    size_t m_timeout; //!< Timeout seconds for outputting data
    uint64_t m_window; //!< Sliding window for outputting data
    uint64_t m_lastEmitted; //!< s_time value of last Buffer emitted
    boost::lockfree::queue<Buffer*, boost::lockfree::fixed_sized<true>> m_inputQueue; //!< Queue pre-sorted data
    std::deque<Buffer*> m_sortedQueue; //!< Queue events sorted by s_time    
    std::unique_ptr<DataSink> m_pSink; //!< Our data sink
    boost::thread m_outThread; //!< Thread for output    
    std::atomic<bool> m_shutdown; //!< Shutdown coordination
    std::condition_variable m_dataReady; //!< Coordinate data ready for output
    std::mutex m_mutex; //!< Condition variable "dummy" mutex
    
public:
    /** @brief Construct from URI */
    BufferedSink(std::string uri, size_t queueSize, size_t timeout=2,
		 size_t window=300);
    /** @brief Destructor */
    ~BufferedSink();

    /**
     * @brief Add data to the input queue
     * @param timestamp Event "timestamp," either nanosecond timestamp 
     * or event count.
     * @param pData Pointer to the data payload
     * @param nBytes Size of payload in bytes
     */
    void addData(uint64_t timestamp, void* pData, size_t nBytes);
    /** @brief Stop threads and signal shutdown */
    void stopThreads();

    /**
     * @brief Set the timeout for outputting data
     * @param timeout The timeout length in seconds for outputting data
     */
    void setTimeout(size_t timeout) { m_timeout = timeout; };
    /**
     * @brief Get the timeout value for outputting data
     * @return The timeout value in seconds
     */
    size_t getTimeout() { return m_timeout; };
    /**
     * @brief Set the sliding window length for determining when to output
     * @param timeout The timeout length in units of Buffer s_time
     */
    void setWindow(size_t window) { m_window = window; };
    /**
     * @brief Get the sliding window length for determining when to output
     * @return The sliding window length in units of Buffer s_time
     */
    size_t getWindow() { return m_window; };

private:
    /**
     * @brief Create the sink from a URI
     * @param uri The sink URI
     * @return Pointer to dynamically created data sink
     * @throw std::runtime_error If the sink protocol is not recognized
     */
    DataSink* makeDataSink(std::string uri);
    /**
     * @brief Insert a buffer into the sorted queue
     * @param pBuffer Pointer to the buffer we're trying to insert
     */
    void insertBuffer(Buffer* pBuffer);
    /** @brief Poll status to output data when ready */
    void poll();
    /** @brief Drain the input queue and sort data into sorted queue */
    void drainInputQueue();
    /**
     * @brief Check if we have data to output due to the sliding window
     * @return True if so, false otherwise
     */
    bool readyEmitFromWindow();
    /** @brief Write out the data ready for outputting */
    void outputData();
    
    // Ring item utilities:
    
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
