/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2015

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Aaron Chester
             FRIB
             Michigan State University
             East Lansing, MI 48824-1321
*/

/** 
 * @file send.cpp
 * @brief Send raw NSCLDAQ data through E2SAR.
 */

/** 
 * @todo (ASC 2/19/25): Better query of LB status if/when we have an 
 * admin token.
 */

#include <iostream>
#include <cstddef>
#include <string>
#include <cstdint>
#include <vector>
#include <csignal>

#include <boost/program_options.hpp>

#include <e2sar.hpp>
#include <e2sarDPSegmenter.hpp>

// NSCLDAQ headers:

#include <DataFormat.h>
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
#include <URL.h>
#include <CDataSourceFactory.h>
#include <CDataSource.h>
#include <CFileDataSource.h>
#include <CRingDataSource.h>

// Project headers:

#include <FribEjfatUtils.h>

namespace po = boost::program_options;
using namespace e2sar;
using namespace frib_ejfat;

// Prepare a pool. To avoid locking the pool we use the return queue.

boost::pool<> *evtBufPool;
boost::lockfree::queue<u_int8_t*> evtBufQueue{10000};

// Other global config:

bool threadsRunning(true);
Segmenter* segPtr{nullptr};
LBManager* lbmPtr{nullptr};       // nullptr if CP is not enabled
std::vector<std::string> senders; // Empty if CP is not enabled

/**
 * @brief Shutdown the sender. Remove senders. Stop threads.
 */
void
shutdown() {
    std::cout << "Stopping threads" << std::endl;
    threadsRunning = false;
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);
    
    if (segPtr != nullptr) {
        if (lbmPtr != nullptr) {
            std::cout << "Removing senders: ";
            for (auto s: senders)
                std::cout << s << " ";
            std::cout << std::endl;
            auto rv = lbmPtr->removeSenders(senders);
            if (rv.has_error()) {
                std::cerr << "Unable to remove sender from list on exit: "
			  << rv.error().message() << std::endl;
	    }
	    delete lbmPtr;
        }
        segPtr->stopThreads();
	delete segPtr;
    }

    boost::this_thread::sleep_for(duration);
}

/**
 * @brief Handle interrupt and shutdown.
 * @note Re-raises default interrupt signal after shutdown for cleanup,
 *   closing data sink, etc.
 */
void
ctrlCHandler(int sig) 
{
    shutdown();
    signal(sig, SIG_DFL);
    raise(sig);    
}

/**
 * @brief Parse the command line variables and return the map.
 * @return Variables map.
 */
po::variables_map
getOpts(int ac, char* av[])
{       
    po::options_description od("Send command-line options");
    od.add_options()
	("help,h", "show command help")
	("config-file,c",
	 po::value<std::string>()->default_value("./segmenter_config.ini"),
	 "path to configuration file")
	("uri,u",
	 po::value<std::string>(),
	 "URI from the command line to override EJFAT_URI envvar")
	("num,n",
	 po::value<size_t>(),
	 "number of events to send, if not specified: send all events")
	("bufsize,b",
	 po::value<size_t>()->default_value(1024*1024),
	 "event buffer size in bytes")
	("send-rate,r",
	 po::value<float>()->default_value(1),
	 "Event send rate in Gbps")
	("ip",
	 po::value<std::string>()->default_value("35.11.82.130"),
	 "IP address (IPv4 or IPv6) from which sender sends from")
	("source,s",
	 po::value<std::string>()->required(),
	 "Input data URI (file or stream only)")
	("nscldaq-version,v",
	 po::value<int>()->default_value(12),
	 "NSCLDAQ data format major version number")
	("debug", "enable debugging output")
	;
    
    po::variables_map vm; // Command line options stored here.
    po::store(po::parse_command_line(ac, av, od), vm);

    if (vm.count("help"))
    {
        std::cout << od << std::endl;
        exit(EXIT_SUCCESS);
    }
    
    po::notify(vm);

    return vm;
}

/** 
 * @brief Callback function to return a buffer to the pool.
 * @param a Buffer to free and return.
 */
void
freeBuffer(boost::any a) 
{
    auto p = boost::any_cast<u_int8_t*>(a);
    evtBufQueue.push(p);
}

/**
 * @breif Add data to the send queue and send it.
 * @param s References our Segmenter instance.
 * @param pSource Pointer to data source where we get ring items.
 * @param nEvents Number of events to send.
 * @param evtBufSize Size of each event buffer, must be big enough to hold a 
 *   single ring item.
 * @param rateGbps Send rate in Gbps (optional, default=1.0)
 * @param debug Show debugging output (optional, default=false)
 * @return EXIT_SUCCESS if successful, E2SAR error otherwise
 */
result<int>
sendEvents(Segmenter &s, CDataSource* pSource, size_t nEvents,
	   size_t evtBufSize, float rateGbps=1.0, bool debug=false)
{
    // Convert bit rate to event rate. Sleep at least 1 us between sends:
    
    float eventRate{rateGbps*1000000000/(evtBufSize*8)};
    u_int64_t interEventSleepUsec{
	static_cast<u_int64_t>(evtBufSize*8/(rateGbps*1000))
    };
    if (interEventSleepUsec == 0) { // Max send rate 1 MHz
	interEventSleepUsec = 1;
    }			 

    std::cout.imbue(std::locale(""));
    std::cout << "Sending bit rate is " << rateGbps << " Gbps" << std::endl;
    std::cout << "Event size is " << evtBufSize << " bytes or "
	      << evtBufSize*8 << " bits" << std::endl;
    std::cout << "Event rate is " << eventRate << " Hz" << std::endl;
    std::cout << "Inter-event sleep time is " << interEventSleepUsec
	      << " microseconds" << std::endl;
    std::cout << "Sending " << nEvents << " event buffers" << std::endl;
    std::cout << "Using MTU " << s.getMTU() << std::endl;
    
    // Start threads, open sockets. Start sending sync packets:
    
    auto rv = s.openAndStart();    
    if (rv.has_error()) {
	std::cerr << "Failed to start Segmenter: "
		  << rv.error().message() << std::endl;
        return rv;
    } else {
	std::cout << "Segmenter started OK\n";
    }	

    // Create our buffer pool:
	
    evtBufPool = new boost::pool<>{evtBufSize};

    // Sleep to allow small number of frames to leave:
    
    boost::chrono::seconds duration(1);
    boost::this_thread::sleep_for(duration);
    
    /////////////////////////////////////////////////////////////////////////
    // Send loop
    //

    u_int8_t* evtBuf{nullptr}; // Buffer from pool - fill and send.
    
    auto now = boost::chrono::high_resolution_clock::now();
    
    if (debug) {
	std::cout << "Starting send loop at: " << now << std::endl;
    }

    // Data we set for each event:
    
    EventNum_t evtNumber = 0; // Iterate on send.
    u_int16_t  dataId    = 0; // Default.
    u_int16_t  entropy   = 0; // Default.
    
    while (1) {
	
	now = boost::chrono::high_resolution_clock::now();
	
	if (!evtBufQueue.pop(evtBuf)) {
	    evtBuf = static_cast<u_int8_t*>(evtBufPool->malloc());
	}

	std::unique_ptr<CRingItem> pItem(pSource->getItem());
	
	if (!pItem.get()) { // End of source.
	    break;
	}
	
	/////////////////////////////////////////////////////////////////////
	// Extract information from the event, copy it into the event buffer,
	// and add it to the send queue. The ring item body of a physics event 
	// has the following contents:
	//
	// +-------------------------------------------------------+
	// | uint32_t - Size of the body in 16 bit words           |
	// +-------------------------------------------------------+
	// | uint32_t - Module ID (bit 21 set: use external clock) |
	// +-------------------------------------------------------+
	// | double   - Clock scale factor                         |
	// +-------------------------------------------------------+
	// | Soup of hits as they come from the module             |
	// | ...                                                   |
	// +-------------------------------------------------------+
	// 
	// Note that there is no body header for raw data, so we set the
	// event number manually rather than using the event's timestamp
	// value. We cannot use the Segmenter's internal counter because
	// we need to supply a callback function to manage our buffer pool.
	///
	    
	uint32_t evtBufSize = pItem->size(); // Bytes.
	memcpy(evtBuf, pItem->getItemPointer(), evtBufSize);
	
	if (debug) {
	    std::cout << "Sending event:" << std::endl;
	    std::cout << "\tevtNumber:  " << evtNumber << std::endl;
	    std::cout << "\tdataId:     " << dataId << std::endl;
	    std::cout << "\tevtBufSize: " << evtBufSize << std::endl;
	    std::cout << "\titem type:  " << pItem->type() << std::endl;
	    //std::cout << pItem->toString() << std::endl;
	}
	    
	rv = s.addToSendQueue(evtBuf, evtBufSize, evtNumber, dataId,
			      entropy, &freeBuffer, evtBuf);
	if (rv.has_error()) {
	    std::cout << rv.error().message() << std::endl;
	    continue;
	}
	
	auto until = now + boost::chrono::microseconds(interEventSleepUsec);
	if (now > until)
	{
	    return E2SARErrorInfo{E2SARErrorc::LogicError, 
		"Clock overrun, either event buffer length too short or "
		"requested sending rate too high"};
	}

	// Free the backlog of unused buffers:
	
	u_int8_t* item{nullptr};
	while (evtBufQueue.pop(item)) {
	    evtBufPool->free(item);
	}

	// Done with this iteration:
	
	evtNumber++;

	// Check if we've hit a limit:
	
	if (nEvents != 0 && evtNumber == nEvents) {
	    break;
	}

	// Wait to send next event:
	
	boost::this_thread::sleep_until(until);
		
    } // End of send loop

    // Done sending events, report:
   
    auto stats = s.getSendStats();    
    std::cout << "Completed, " << evtNumber << " events, "
	      << stats.get<0>() << " frames sent, "
	      << stats.get<1>() << " errors" << std::endl;
    if (stats.get<1>() != 0)
    {
        std::cout << "Last error encountered: "
		  << strerror(stats.get<2>()) << std::endl;
    }

    evtBufPool->purge_memory();
    
    return EXIT_SUCCESS;
}

/**
 * @brief Send main. Create a data source and Segmenter; send data.
 */
int
main(int argc, char* argv[])
{
    try {
	
    	/////////////////////////////////////////////////////////////////////
	// Read arguments and setup signal handler
	///
	
	auto opts = getOpts(argc, argv); // Command-line options.
	signal(SIGINT, ctrlCHandler);    // Ctrl-C signal.

	/////////////////////////////////////////////////////////////////////
	// Configure data source
	///
	
	auto srcUri = opts["source"].as<std::string>();
	std::vector<uint16_t> sample, exclude; // Required but unused.
	CDataSourceFactory factory;
	std::unique_ptr<CDataSource> pSource(
	    factory.makeSource(srcUri, sample, exclude)
	    );	

	/////////////////////////////////////////////////////////////////////
	// Configure E2SAR
	///
   
	EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

	u_int32_t evtSourceId(0);
	u_int16_t dataId(0);
	size_t evtBufSize = opts["bufsize"].as<size_t>();
	bool debug = opts.count("debug");
	float rateGbps = opts["send-rate"].as<float>();
	std::string configFile(opts["config-file"].as<std::string>());
	std::string sendIP(opts["ip"].as<std::string>());

	// Override defaults if provided:

	std::string ejfatUri_s("");
	if (opts.count("uri")) {
	    ejfatUri_s = opts["uri"].as<std::string>();
	}
	
	size_t nEvents = 0;	
	if (opts.count("num")) {
	    nEvents = opts["num"].as<size_t>();
	}
	
	auto flags = getSegmenterFlagsFromINI(configFile);
	auto ejfatUri = getURI(ejfatUri_s, tt, flags.dpV6);	    
    
	if (debug) {
	    std::cout << "Using E2SAR version: " << get_Version() << std::endl;
	    printSegmenterFlags(flags);
	    std::cout << "Using URI: " << ejfatUri.to_string() << std::endl;
	    std::cout << "Sending " << nEvents << " events" << std::endl;
	    std::cout << "Max buffer: " << evtBufSize << " bytes" << std::endl;
	}	

	/////////////////////////////////////////////////////////////////////
	// Instantiate and run Segmenter:
	///
	
	// Configure control plane (if used):

	if (flags.useCP) {	    
	    // Note that when using CP the sender IP addr must be one of:
	    // IPv6: 2605:dd00:4000:82:130:1232:2709:0
	    // IPv4: 35.11.82.130
	    
	    senders.push_back(sendIP);
	    
	    lbmPtr = new LBManager(ejfatUri);

	    std::cout << "Adding senders to LB:\n";
	    for (const auto& s: senders) {
		std::cout << s << " ";
	    }
	    std::cout << std::endl;

	    auto rv_as = lbmPtr->addSenders(senders);

	    if (rv_as.has_error()) {
		std::cerr << "Unable to add sender: "
			  << rv_as.error().message() << std::endl;
		std::cerr << "Exiting...\n";
		shutdown();
		std::exit(EXIT_FAILURE);
	    }
	    
	    auto token = EjfatURI::TokenType::session;
	    auto lbmUri_s = lbmPtr->get_URI().to_string(token);
	    auto addrStr = lbmPtr->get_AddrString();
	    auto lbId = lbmPtr->get_URI().get_lbId();
		    
	    std::cout << "Getting LB status:" << std::endl;
	    std::cout << "\tContacting: " << lbmUri_s
		      << " using address: " << addrStr
		      << std::endl;
	    std::cout << "\tLB ID: " << lbId << std::endl;
	}	
	
	segPtr = new Segmenter(ejfatUri, dataId, evtSourceId, flags);
	auto rv = sendEvents(*segPtr, pSource.get(), nEvents,
			     evtBufSize, rateGbps, debug);
	if (rv.has_error()) {
	    std::cerr << "Segmenter encountered an error: "
		      << rv.error().message() << std::endl;
	    exit(EXIT_FAILURE);
	}

    }
    catch (const E2SARException& e) {
	std::cerr << "E2SAR exception: "
		  << static_cast<std::string>(e) << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }
    catch (std::string& e) {
	std::cerr << "std::string exception: " << e << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }
    catch (...) {
	std::cerr << "Caught unexpected exception type, exiting" << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }

    shutdown();
    
    return EXIT_SUCCESS;
}
