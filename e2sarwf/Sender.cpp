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
 * @file Sender.cpp
 * @brief Implementation of the Sender class
 */

#include "Sender.h"

#include <csignal>
#include <filesystem>
#include <iostream>

// E2SAR includes and deps:

#include <e2sar.hpp>

// Unified format library:

#include <DataFormat.h>
#include <RingItemFactoryBase.h>
#include <CRingItem.h>
#include <CAbnormalEndItem.h>
#include <CDataFormatItem.h>
#include <CGlomParameters.h>
#include <CPhysicsEventItem.h>
#include <CRingFragmentItem.h>
#include <CRingPhysicsEventCountItem.h>
#include <CRingScalerItem.h>
#include <CRingTextItem.h>
#include <CRingStateChangeItem.h>
#include <CUnknownFragment.h>

// Other NSCLDAQ includes:

#include <URL.h>
#include <CRemoteAccess.h>
#include <CRingBuffer.h>

// Project headers:

#include "FribE2sarUtils.h"
#include "DataSource.h"
#include "RingDataSource.h"
#include "FdDataSource.h"
#include "StreamDataSource.h"

using namespace e2sar;
using namespace frib_e2sar;
using namespace ufmt;

const double MAX_FILL_PCT = 0.9; //!< Send buffer minimum fill percent

Sender* Sender::m_pInstance = nullptr;

/**
 * @details
 * Constructor throws on error, which the caller is expected to handle. 
 * We parse the variables map, configure the Segmenter and Load Balancer and 
 * report the configuration. Note that your EJFAT URI must reflect whether 
 * or not you are using the Load Balancer e.g., setting `useCP = true` in 
 * the initialization file.
 */
Sender::Sender(po::variables_map& vm) :
    m_rateGbps(vm["rate"].as<float>()),
    m_dataId(vm["dataid"].as<u_int16_t>()),
    m_nEvents(vm["num"].as<size_t>()),
    m_evtBufSize(vm["bufsize"].as<size_t>()),
    m_maxBufBytes(vm["bufsize"].as<size_t>()*MAX_FILL_PCT),
    m_threadsRunning(false),
    m_debug(vm["debug"].as<bool>()),
    m_verbose(vm["verbose"].as<bool>())
{
    /////////////////////////////////////////////////////////////////////////
    // Set instance and register signal handler
    //

    if (m_pInstance) {
	throw std::runtime_error("Sender instance already exists!");
    } else {
	setInstance(this);
	std::signal(SIGINT, ctrlCHandler);
    }
    
    /////////////////////////////////////////////////////////////////////////
    // Create NSCLDAQ data source
    //

    auto daqVersion = vm["nscldaq-version"].as<int>();    
    FormatSelector::SupportedVersions version = mapVersion(daqVersion);
    auto& factory = FormatSelector::selectFactory(version);
    
    auto srcName = vm["source"].as<std::string>();
    m_pSource = std::unique_ptr<DataSource>(makeDataSource(&factory, srcName));

    /////////////////////////////////////////////////////////////////////////
    // Read ini file and get flags
    //

    std::string iniFile;
    if (!vm.count("ini")) {
	auto cwd = std::filesystem::current_path();
	iniFile = cwd.string() + "/segmenter_config.ini";
    }
    
    auto flags = getSegmenterFlagsFromFile(iniFile);

    /////////////////////////////////////////////////////////////////////////
    // Create EJFAT URI
    //
    
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};
    auto ejfatUri = getUri(vm["uri"].as<std::string>(), tt, flags.dpV6);  
    
    /////////////////////////////////////////////////////////////////////////
    // Set E2SAR optimizations
    //

    std::vector<std::string> opts; // Empty vector is 'none'
    if (vm.count("optimize")) {
	opts = vm["optimize"].as<std::vector<std::string>>();
    }
    auto rvopt = Optimizations::select(opts);
    if (rvopt.has_error()) {
	std::string msg("Failed to select optimization level: ");
	msg += rvopt.error().message();
	throw std::runtime_error(msg);
    }
    
    /////////////////////////////////////////////////////////////////////
    // Configure control plane
    //
    
    auto sendIp = vm["ip"].as<std::string>();
    
    if (flags.useCP) {
	// for daq-ejfat-01 when using CP the sender IP addr must be one of:
	//     IPv6: 2605:dd00:4000:82:130:1232:2709:0
	//     IPv4: 35.11.82.130
	
	m_senders.push_back(sendIp);	
	m_pLBManager = std::make_unique<LBManager>(ejfatUri);
	
	std::cout << "Adding senders to LB:\n";
	for (const auto& s: m_senders) {
	    std::cout << s << " ";
	}
	std::cout << std::endl;
	
	auto rvas = m_pLBManager->addSenders(m_senders);
	if (rvas.has_error()) {
	    std::string msg("Unable to add sender: ");
	    msg += rvas.error().message();
	    throw std::runtime_error(msg);
	}
	
	auto token = EjfatURI::TokenType::session;
	if (m_verbose) {
	std::cout << "Getting LB status:" << std::endl;
	std::cout << "\tContacting: "
		  << m_pLBManager->get_URI().to_string(token)
		  << " using address: " << m_pLBManager->get_AddrString()
		  << std::endl;
	std::cout << "\tLB ID: " << m_pLBManager->get_URI().get_lbId()
		  << std::endl;
	}
    }

    /////////////////////////////////////////////////////////////////////
    // Instantiate Segmenter and buffer queue
    //

    auto srcId = vm["srcid"].as<u_int32_t>();
    auto queueSize = vm["queue-size"].as<size_t>();
    
    m_pSegmenter
	= std::make_unique<Segmenter>(ejfatUri, m_dataId, srcId, flags);
    m_pEvtBufQueue
	= std::make_unique<boost::lockfree::queue<u_int8_t*>>(queueSize);

    if (m_verbose) {
	std::cout << "----- Sender configuration -----" << std::endl;
	printSegmenterFlags(flags);
	std::cout << "Using URI:   " << ejfatUri.to_string() << std::endl;
	if (m_nEvents > 0) {
	    std::cout << "Sending:     " << m_nEvents << " events" << std::endl;
	} else {
	    std::cout << "Sending:     all events" << std::endl;
	}
	std::cout << "Data ID:     " << m_dataId << std::endl;
	std::cout << "Source ID:   " << srcId << std::endl;
	std::cout << "evtBufSize:  " << m_evtBufSize << " bytes" << std::endl;
	std::cout << "maxBufBytes: " << m_maxBufBytes << " bytes " << std::endl;
	std::cout << "sendRate:    " << m_rateGbps << " Gbps" << std::endl;
	std::cout << "Queue size   " << queueSize << std::endl;
	std::cout << "E2SAR selected optimizations:  "
		  << concatWithSeparator(Optimizations::selectedAsStrings())
		  << std::endl;
	std::cout << "NSCLDAQ format version:        " << daqVersion
		  << std::endl;
	std::cout << "--------------------------------" << std::endl;
    }
}

/**
 * @details
 * Calls class `shutdown()` method
 */
Sender::~Sender()
{
    shutdown();
}

/**
 * @details
 * Pack multiple ring items into a single send buffer. Generally a ring item 
 * is on the order of 100-10k bytes, but our send buffer defaults to 1 MB. 
 * Ring items are added until the buffer is filled to at least 90% of its 
 * capacity. We don't really care what the data looks like once we fish it 
 * out of the source - the ring items know their size and we do a byte-by-byte
 * copy into the event buffer. Events are numbered sequentially NOT by 
 * timestamp!
 */
int
Sender::operator()()
{   
    // Convert bit rate to event rate. Sleep at least 1 us between sends:
    float eventRate{m_rateGbps*1000000000/(m_evtBufSize*8)};
    u_int64_t interEventSleepUsec{
	static_cast<u_int64_t>((m_evtBufSize*8)/(m_rateGbps*1000))
    };
    if (interEventSleepUsec == 0) { // Max send rate 1 MHz
	interEventSleepUsec = 1;
    }

    if (m_verbose) {
	std::cout << "Inter-event sleep time is " << interEventSleepUsec
		  << " microseconds" << std::endl;
    }

    // Start threads, open sockets. Start sending sync packets:

    auto rvoas = m_pSegmenter->openAndStart();    
    if (rvoas.has_error()) {
	std::cerr << "Failed to start Segmenter: Error code "
		  << rvoas.error().code() << " " << rvoas.error().message()
		  << std::endl;
        return -1;
    } else {
	std::cout << "Segmenter started OK\n";
	m_threadsRunning = true;
    }

    // Sleep to allow small number of frames to leave:
    
    boost::chrono::seconds duration(1);
    boost::this_thread::sleep_for(duration);

    // Create our buffer pool:
	
    auto pEvtBufPool = std::make_unique<boost::pool<>>(m_evtBufSize);
    u_int8_t* evtBuf{nullptr}; // Buffer from pool - fill and send
    
    if (m_verbose) {
	std::cout << "Starting send loop at: "
		  << boost::chrono::high_resolution_clock::now()
		  << std::endl;
    }

    /////////////////////////////////////////////////////////////////////////
    // Send loop
    //

    EventNum_t evtNumber = 0;  // Iterate on send
    u_int16_t  entropy = 0;    // Random
    size_t     totalBytes = 0; // Total bytes sent
    bool       done = false;

    while (!done) {
	auto now = boost::chrono::high_resolution_clock::now();
	
	if (!m_pEvtBufQueue->pop(evtBuf)) {
	    evtBuf = static_cast<u_int8_t*>(pEvtBufPool->malloc());
	}

	// Pack ring items into the event buffer:

	u_int8_t* p = evtBuf;    // Pointer to first byte
	size_t currentBytes = 0; // Bytes in send buffer

	while (currentBytes < m_maxBufBytes) {
	    std::unique_ptr<CRingItem> pItem(m_pSource->getItem());
	    if (!pItem.get()) {
		done = true;  // End of source
		break;
	    }
	    uint32_t itemSize = pItem->size();
	    memcpy(p, pItem->getItemPointer(), itemSize);
	    currentBytes += itemSize;
	    p += itemSize; // Prepare to copy next item
	} // End buffer packing
	
	auto rvseg = m_pSegmenter->addToSendQueue(evtBuf, currentBytes,
						  evtNumber, m_dataId, entropy,
						  &senderCallback, evtBuf);
	if (rvseg.has_error()) {
	    std::cerr << "Failed to add to send queue: "
		      << rvseg.error().message()
		      << " with error code " << rvseg.error().code()
		      << " trying to continue..."
		      << std::endl;
	    continue;
	}

	totalBytes += currentBytes;
	
	if (m_debug) {
	    // dumpBuffer(evtBuf, currentBytes);
	    std::cout << "Sent event:" << std::endl;
	    std::cout << "\tevtNumber:      " << evtNumber << std::endl;
	    std::cout << "\tdataId:         " << m_dataId << std::endl;
	    std::cout << "\tevtBufSize:     " << currentBytes << std::endl;
	    std::cout << "\tmaxBufBytes:    " << m_maxBufBytes << std::endl;
	    std::cout << "\tevtBufCapacity: " << m_evtBufSize << std::endl;
	    std::cout << "\ttotalBytes:     " << totalBytes << std::endl;
	}
	
	auto until = now + boost::chrono::microseconds(interEventSleepUsec);
	if (now > until) {
	    std::cerr << "Clock overrun, either event buffer length too "
		      << "short or requested sending rate too high"
		      << std::endl;
	    return -1;
	}

	// Free the backlog of unused buffers:
	
	u_int8_t* item{nullptr};
	while (m_pEvtBufQueue->pop(item)) {
	    pEvtBufPool->free(item);
	}

	// Done with this iteration:
	
	evtNumber++;

	// Check if we've hit a send limit:

	if (m_nEvents != 0 && evtNumber == m_nEvents) { done = true; }
	
	// Wait to send next event:
    
	boost::this_thread::sleep_until(until);
    }

    // Done sending events, report:

    auto stats = m_pSegmenter->getSendStats();
    std::cout << "Completed, " << stats.msgCnt
	      << " frames sent, " << stats.errCnt << " errors"
	      << std::endl;
    if (stats.errCnt != 0) {
        if (stats.lastE2SARError != E2SARErrorc::NoError) {
            std::cout << "Last E2SARError code: "
		      << make_error_code(stats.lastE2SARError).message()
		      << std::endl;
        } else
            std::cout << "Last error encountered: "
		      << strerror(stats.lastErrno)
		      << std::endl;
    }

    // Cleaup pool:
    
    pEvtBufPool->purge_memory();
	  
    return 0;
}

/**
 * @details 
 * Re-raises default signal after shutdown
 */
void
Sender::ctrlCHandler(int sig) 
{
    if (m_pInstance) {
	m_pInstance->shutdown();
    }
    std::signal(sig, SIG_DFL);
    raise(sig);
}

/****************************************************************************
 * Private functions                                                        *
 ***************************************************************************/

void
Sender::shutdown()
{
    std::cout << "Stopping sender threads..." << std::endl;
    m_threadsRunning = false;
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);

    if (m_pSegmenter) {
	if (m_pLBManager) {
	    std::cout << "Removing senders: ";
            for (auto s: m_senders) {
                std::cout << s << " ";
	    }
            std::cout << std::endl;
            auto rv = m_pLBManager->removeSenders(m_senders);
            if (rv.has_error()) {
                std::cerr << "Unable to remove sender from list on exit: "
			  << rv.error().message() << std::endl;
	    }
	}
	m_pSegmenter->stopThreads();
    }

    boost::this_thread::sleep_for(duration);
}

FormatSelector::SupportedVersions
Sender::mapVersion(int vsn)
{
    switch (vsn) {
    case 12:
	return FormatSelector::v12;
    case 11:
	return FormatSelector::v11;
    case 10:
	throw std::invalid_argument("NSCLDAQ 10 is not currently supported");
    default:
	throw std::invalid_argument("Invalid DAQ format version specifier");
    }
}

DataSource*
Sender::makeDataSource(RingItemFactoryBase* pFactory,
			const std::string& strUrl)
{
    // Special case the url is just "-" then it's stdin, a file descriptor
    // data source:
    
    if (strUrl == "-") {
        return new FdDataSource(pFactory, STDIN_FILENO);   
    }
        
    URL uri(strUrl);
    std::string proto = uri.getProto();
    
    if (proto == "tcp" || proto == "ring") {
	CRingBuffer* pRing = CRingAccess::daqConsumeFrom(strUrl);
	return new RingDataSource(pFactory, *pRing);
    } else if (proto == "file") {
        std::string path = uri.getPath();
	// Need it to last past block:
        std::ifstream& in(*(new std::ifstream(path.c_str())));
	if (!in.good()) {
	    std::string msg("Failed to create input stream from ");
	    msg += path;
	    throw std::invalid_argument(msg);	    
	}
        return new StreamDataSource(pFactory, in);
    } else { // Last chance fallback for malformed URI
	std::string msg("Unrecognized source URI protocol '");
	msg += proto + "'";
	throw std::invalid_argument(msg);
    }
}

void
Sender::senderCallback(boost::any a) 
{
    m_pInstance->freeBuffer(a);
}

void
Sender::freeBuffer(boost::any a) 
{
    auto p = boost::any_cast<u_int8_t*>(a);
    m_pEvtBufQueue->push(p);
}
