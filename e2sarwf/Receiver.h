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

     Author note: This code draws heavily from e2sar_perf.cpp which was 
                  written by the E2SAR collaboration. The source code and 
                  license for the E2SAR collaboration software can be 
                  found at: https://github.com/JeffersonLab/E2SAR
		  --ASC 5/1/25
*/

/**
 * @file Receiver.h
 * @brief Defines a class to receive data through EJFAT/E2SAR
 */

#ifndef RECEIVER_H
#define RECIEVER_H

#include <memory>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/thread.hpp>

#include <NSCLDAQFormatFactorySelector.h>

namespace e2sar {
    class Reassembler;
}
class BufferedSink;

/**
 * @class Receiver
 * @brief Receive data via EJFAT/E2SAR
 * @details
 * Listen for data and write it to data sink(s). Each dequeue thread writes 
 * to its own sink (ringbuffer or file). Each event buffer recieved by this 
 * class is internally ordered as the data appeared in the source. However, 
 * buffers may arrive out-of-order and data in the sink(s) do not preserve 
 * the source ordering. This class runs an E2SAR Reassembler in non-blocking
 * mode to grab the data.
 * @todo (ASC 5/6/25): Receiver must be coupled to orderer(s) to re-sort data. 
 * Glom can be used to merge and output a single stream of built events for 
 * raw data. For data which is already built, the process of ordering and 
 * merging the streams is still under investigation (guts of ddasSort? 
 * Orderer?)
 */

class Receiver
{
private:
    std::string m_proto;    //!< Protocol for data sink
    std::string m_hostName; //!< Hostname for ringbuffer data sink
    std::string m_basePath; //!< Base path for file data sink
    std::string m_sinkName; //!< Base name of sink
    int m_duration;         //!< Run duration in seconds
    size_t m_numThreads;    //!< Number of dequeue threads reading data
    bool m_threadsRunning;  //!< True while running
    bool m_debug;           //!< Enable debugging output

    std::vector<boost::thread> m_deqThreads; //!< Dequeue threads

    std::unique_ptr<e2sar::Reassembler> m_pReassembler; //!< E2SAR Reassembler
    std::unique_ptr<BufferedSink> m_pSink; //!< Sink where data is written
    
    static Receiver* m_pInstance; //!< Part of the signal-handling interface
    
public:
    /**
     * @brief Constructor
     * @param vm References the variables map used to configure the class
     * @throw std::runtime_error A receiver instance already exists
     */
    Receiver(boost::program_options::variables_map& vm);
    /** @brief Destructor */
    ~Receiver();

    /**
     * @brief Run the event loop
     * @throw std::runtime_error Failure to initialize or start Reassembler
     * @return int
     * @retval EXIT_SUCCESS Success
     * @retval EXIT_FAILURE Failure, hopefully with error message on stderr
     */
    int operator()();
    
    /**
     * @brief Part of the signal-handling interface: handle Ctrl-C interrupt 
     * and shutdown safely
     * @param sig Signal to handle (expected to be SIGINT)
     * @note (ASC 5/6/25): This method must be a static method with C linkage 
     * but should be considered, practically speaking, internal to the class 
     * itself. It uses the singleton-like instance variable to call the class' 
     * shutdown method. Not recommended to call this method externally.
     */ 
    static void ctrlCHandler(int sig);

private:
    /**
     * @brief Part of the signal-handling interface: set instance for handler
     * @param r Pointer to instance
     */ 
    static void setInstance(Receiver* r) { m_pInstance = r; };
    /** @brief Shutdown the receiver. Deregister workers. Stop threads. */
    void shutdown();
    /**
     * @brief Monitor Reassembler stats while running. 
     */
    void statsThread();
    /**
     * @brief Register workers, open and start the Reassembler.
     * @return int
     * @retval EXIT_SUCCESS Success
     * @retval EXIT_FAILURE Failure, hopefully with error message on stderr
     */
    int prepareToReceive();
    /**
     * @brief Receive and process data
     * @return int
     * @retval EXIT_SUCCESS Success
     * @retval EXIT_FAILURE Failure, hopefully with error message on stderr
     */
    int receiveEvents();
    /**
     * @brief Create a sink URI from a string
     * @return URI string for generic data sink
     */
    std::string makeSinkUri();
};

#endif
