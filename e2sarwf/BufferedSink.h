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
 * @brief Provides a class for managing a data sink with some buffering to 
 * ensure time-ordered data is in the processing pipeline.
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
     * Free malloc'd memory pointed to by pData 
     */
    ~Buffer() { delete[] static_cast<uint8_t*>(s_pData); };
};

/**
 * @class BufferedSink
 * @brief A class for managing a DataSink object and buffering data into it. 
 * This implements a very simple multiple-producer, single-consumer model 
 * where multiple (external) threads (from the Reassembler side) may be adding
 * data to the input queue but only the output thread can access the sorted 
 * queue and write data to the sink.
 *
 * @note If the input queue size exceeds its initial capacity, it may be 
 * resized. There is no guarantee this resize operation is lock-free.
 *
 * @note (ASC 9/11/25): A single thread performs the sorting and outputting of 
 * data, which is "good enough" for simple workflows. A more complex threaded 
 * design may be desireable to separate the operation of moving data from the 
 * input to the sorted queue from the actual I/O but for current applicaions 
 * the performance is OK without it. Note that the condition varible and sort 
 * queue are protected by different mutexes, anticipating this change.
 *
 * @note (ASC 9/18/25): NSCLDAQ swtrigger CRingBufferTransportEJFAT class has 
 * a read timeout to ensure that all the data is processed through the 
 * EventEditor workers properly. This means this class needs a timeout window 
 * shorter than the timeout in CRingBufferTransportEJFAT or enough data 
 * through the pipleline such that the output is triggered by the window limit 
 * prior to the read timing out. Generally this is an issue at the start of a 
 * run or if the chunk size fanned out to each worker is fairly large (>10k) 
 * as this introduces longer processing delays.
 */

class BufferedSink
{
private:
    size_t m_timeoutMs;           //!< Timeout seconds for outputting data
    uint64_t m_window;            //!< Sliding window for outputting data
    uint64_t m_lastEmitted;       //!< s_time value of last Buffer emitted
    std::atomic<bool> m_shutdown; //!< Shutdown coordination
    size_t m_queueCapacity;       //!< Track input queue capacity 
    boost::lockfree::queue<Buffer*> m_inputQueue; //!< Queued pre-sorted data
    std::deque<Buffer*> m_sortedQueue;    //!< Queued events sorted by s_time
    std::unique_ptr<DataSink> m_pSink;    //!< Our data sink    
    boost::thread m_outThread;            //!< Thread for output    
    std::condition_variable m_inputReady; //!< Coordinate data ready
    std::mutex m_inputMutex;              //!< m_inputReady.wait_for() mutex
    std::mutex m_sortMutex;               //!< Mutex for accessing sort queue
    
public:
    /** 
     * @brief Constructor
     * @param uri Sink URI (file:// or ringbuffer tcp://)
     * @param useCt If true, use event counter as event number (default=false)
     */
    BufferedSink(std::string uri, bool useCt=false);
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
     * @param timeoutMs The timeout length in milliseconds for outputting data
     */
    void setTimeout(size_t timeoutMs) { m_timeoutMs = timeoutMs; };
    /**
     * @brief Get the timeout value for outputting data
     * @return The millisecond timeout value
     */
    size_t getTimeout() { return m_timeoutMs; };
    /**
     * @brief Set the sliding window length for determining when to output
     * @param timeout The timeout length in units of Buffer s_time
     * @warning It is up to the caller to ensure the window is in the proper 
     * units (timestamps or number of buffers)
     */
    void setWindow(size_t window) { m_window = window; };
    /**
     * @brief Get the sliding window length for determining when to output
     * @return The sliding window length in units of Buffer s_time
     */
    size_t getWindow() { return m_window; };
    /**
     * @brief Set input queue capacity
     * @param size_t capacity The desired queue capacity
     * @note Sets variable to track the queue capacity to the input value
     */
    void setInputQueueCapacity(size_t capacity) {
	m_queueCapacity = capacity;
	m_inputQueue.reserve(m_queueCapacity);
    };
    /**
    * @brief Get the queue capacity
    * @return Input queue capacity
    * @note `capacity()` is private for boost lockfree queues, capacity is 
    * tracked with a variable which is returned here
    */
    size_t getInputQueueCapacity() { return m_queueCapacity; };

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
    /** 
     * @brief Drain the input queue and insert data into the sorted queue
     * @return True if new data is available, false otherwise
     */
    bool drainAndSort();
    /**
     * @brief Checks if the sort queue has data possibly ready for outputting
     * @return True if the sorted queue contains any data, false otherwise 
     */
    bool haveSortedData();
    /**
     * @brief Check if we have data to output due to the sliding window
     * @return True if so, false otherwise
     */
    bool emitFromWindow();
    /** 
     * @brief Write out the data ready for outputting 
     */
    void outputData(bool flush=false);
    
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
