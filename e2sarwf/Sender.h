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

     Author note: This code is heavily based on e2sar_perf.cpp provided as 
                  an example by the E2SAR collaboration. The source code and 
		  lisence for the E2SAR collaboration software can be found 
		  at: https://github.com/JeffersonLab/E2SAR
		  --ASC 5/1/25
*/

/**
 * @file Sender.h
 * @brief Defines a class to send data through EJFAT/E2SAR
 */

#ifndef SENDER_H
#define SENDER_H

#include <string>
#include <memory>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/lockfree/lockfree_forward.hpp>

#include <NSCLDAQFormatFactorySelector.h>

namespace e2sar {
    class Segmenter;
    class LBManager;
}
class DataSource;
namespace ufmt {
    class RingItemFactoryBase;
}
namespace po = boost::program_options;

/**
 * @class Sender
 * @brief Send data through EJFAT/E2SAR.
 * @details 
 * This class defines a functor to send data through EJFAT/E2SAR. The event 
 * loop runs inside the class' `operator()`. Data is sent via an instance of 
 * the E2SAR Segmenter running in non-blocking mode. Configuration of the 
 * object is handled through the constructor. The static method
 * `ctrlCHandler()` exists to allow safe shutdown on SIGINT and in general 
 * should not be considered part of the public interface to this class. 
 */

class Sender
{    
private:
    float m_rateGbps;      //!< Send rate in Gbps
    u_int16_t m_dataId;    //!< Data Id
    size_t m_nEvents;      //!< Number of events to send (0 until eof or SIGINT)
    size_t m_evtBufSize;   //!< Send buffer size in bytes
    size_t m_maxBufBytes;  //!< Max bytes to fill before sending
    bool m_threadsRunning; //!< True when send loop is active
    bool m_debug;          //!< Output debugging information
    bool m_verbose;        //!< Enable verbose output of configuration, etc.
    std::vector<std::string> m_senders; //!< List of sender IP addresses
    
    std::unique_ptr<e2sar::Segmenter> m_pSegmenter; //!< E2SAR Segmenter
    std::unique_ptr<e2sar::LBManager> m_pLBManager; //!< E2SAR Load Balancer
    std::unique_ptr<DataSource> m_pSource; //!< Source of data to send
    /** Buffer queue to recycle send buffers */
    std::unique_ptr<boost::lockfree::queue<u_int8_t*>> m_pEvtBufQueue;

    static Sender* m_pInstance; //!< Instance for handling signals
    
public:
    /**
     * @brief Constructor
     * @param vm References the variables map used to configure the class
     * @throw std::runtime_error A Sender instance already exists
     * @throw std::runtime_error Optimization level cannot be set
     * @throw std::runtime_error Cannot add sender to Load Balancer
     * @throw Any E2SAR errors, etc. back to caller
     */ 
    Sender(po::variables_map& vm);
    /**
     * @brief Destructor
     */
    ~Sender();

    /**
     * @brief Run the event loop
     * @return int
     * @retval 0 Success
     * @retval -1 Failure (hopefully with contextual error message on stderr)
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
     * @param s Pointer to instance
     */ 
    static void setInstance(Sender* s) { m_pInstance = s; };
    /** @brief Shutdown the sender. Remove senders. Stop threads. */
    void shutdown();
    /**
     * @brief Map the version we get from the command line to a factory version
     * @param vsn Format the user requested
     * @throw std::invalid_argument Bad format version
     * @return Factory version ID (from the enum)
     */
    ufmt::FormatSelector::SupportedVersions mapVersion(int vsn);
    /**
     * @brief Parse the URI of the source and based on the parse create the 
     * underlying connection. Create the correct concrete instance of 
     * DataSource given all that
     * @param pFactory Pointer to the ring item factory to use
     * @param strUrl   String URI of the connection
     * @throw std::invalid_argument Unknown source protocol
     * @throw std::invalid_argument Cannot open file data source
     * @throw CException Cannot create ringbuffer data source
     * @return Dynamically allocated data source
     */
    DataSource* makeDataSource(ufmt::RingItemFactoryBase* pFactory,
			       const std::string& strUrl);
    /** 
     * @brief Static callback function for Segmenter non-blocking send using 
     * `addToSendQueue()`
     * @param a Buffer to return to the queue
     * @note (ASC 5/6/25): Static with C linkage to satisfy callback 
     * requirements. Possibly some std::bind business can avoid using the 
     * instance pointer?
     */
    static void senderCallback(boost::any a);
    /** 
     * @brief Callback function to return a buffer to the pool
     * @param a Buffer to return to the queue
     */
    void freeBuffer(boost::any a);
};

#endif
