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
#include <deque>
#include <memory>
#include <mutex>
#include <string>

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
 */

class BufferedSink
{
private:
    size_t m_timeout; //!< Timeout seconds for outputting data
    uint64_t m_window; //!< Sliding window for outputting data
    uint64_t m_lastEmitted; //!< s_time value of last Buffer emitted
    std::deque<Buffer*> m_evtList; //!< Queue of events sorted by timestamp
    std::unique_ptr<DataSink> m_pSink; //!< Our data sink
    std::unique_ptr<boost::thread> m_pOutThread; //!< Thread for output
    std::mutex m_mutex; //!< Mutex for locking container access
    
public:
    /** 
     * @brief Construct from URI 
     * @param uri Sink URI string
     * @param timeout Timeout to flush queue in seconds (default=2)
     * @param window Queue depth in timestamp units for flush (default=300)
     */
    BufferedSink(std::string uri, size_t timeout=2, size_t window=300);
    /** @brief Destructor */
    ~BufferedSink();

    /**
     * @brief Add data to the queue for writing to the sink
     * @param timestamp Event timestamp
     * @param pData Pointer to the data buffer
     * @param nBytes Size of data buffer in bytes
     */
    void addData(uint64_t timestamp, void* pData, size_t nBytes);
    /** @brief Stop and join output thread, do final flush of queue to sink */
    void stopThreads();

    /**
     * @brief Set the timeout
     * @param timeout Timeout length in seconds
     */
    void setTimeout(size_t timeout) { m_timeout = timeout; };
    /**
     * @brief Get the timeout
     * @return Timeout length in seconds
     */
    size_t getTimeout() { return m_timeout; };
    /**
     * @brief Set the queue emission window
     * @param window Window size in timestamp units (timestamp or event count)
     */
    void setWindow(size_t window) { m_window = window; };
    /**
     * @brief Get the queue emission window
     * @return Window size in timestamp units (timestamp or event count)
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
     * @brief Insert a buffer into the queue for outputting
     * @param pBuffer Pointer to the buffer we're inserting
     */
    void insertBuffer(Buffer* pBuffer);
    /** @brief Check if data is ready to be output and output it if so */
    void poll();
    /** @brief Write data to the sink */
    void outputData();
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
