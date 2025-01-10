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
 * @brief Send simulated DDAS data through E2SAR.
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

// Unified format library:

#include <NSCLDAQFormatFactorySelector.h>
#include <DataFormat.h>
#include <RingItemFactoryBase.h>

// These are headers for the abstrct ring items we can get back from the
// factory. As new ring items are added this set of #include's must be
// updated as well as any processing steps.

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

// Other NSCLDAQ headers:

#include <URL.h>
#include <Exception.h>

// Project headers:

#include "DataSource.h"
#include "FdDataSource.h"
#include "StreamDataSource.h"
#include "EjfatUtilities.h"

namespace po = boost::program_options;
using namespace e2sar;
using namespace ufmt;
using namespace e2sarUtils; // Our utilities

// Prepare a pool. To avoid locking the pool we use the return queue.

boost::pool<> *evtBufPool;
boost::lockfree::queue<u_int8_t*> evtBufQueue{10000};

// Other global config:

bool threadsRunning(true);
Segmenter* segPtr{nullptr};
LBManager* lbmPtr{nullptr};       // nullptr if CP is not enabled
std::vector<std::string> senders; // Empty if CP is not enabled

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
            auto rmres = lbmPtr->removeSenders(senders);
            if (rmres.has_error()) {
                std::cerr << "Unable to remove sender from list on exit: "
			  << rmres.error().message() << std::endl;
	    }
	    delete lbmPtr;
        }
        segPtr->stopThreads();
	delete segPtr;
    }

    boost::this_thread::sleep_for(duration);
}

/**
 * @brief Handle interrupt and shutdown nicely.
 * @note Re-raises default interrupt signal at end. Should clean up, close 
 *   files, etc.
 */
void
ctrlCHandler(int sig) 
{    // Re-raise the signal and invoke default behavior:
    shutdown();
    signal(sig, SIG_DFL);
    raise(sig);    
}

/**
 * @brief Parse the command line variables and return the map.
 * @return Variables map
 */
po::variables_map
getOpts(int ac, char* av[])
{   
    // Configure and parse command-line options:
    
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
 * @brief Map the version we get from the command line to a factory version.
 * @param fmtIn Format the user requested.
 * @throw std::invalid_argument Bad format version, including NSCLDAQ v10.
 * @return Factory version ID (from the enum).
 */
FormatSelector::SupportedVersions
mapVersion(int fmtIn)
{
    switch (fmtIn) {
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

/** @brief Callback function to return a buffer to the pool */
void
freeBuffer(boost::any a) 
{
    auto p = boost::any_cast<u_int8_t*>(a);
    evtBufQueue.push(p);
}

/**
 * @breif Add data to the send queue and send it.
 * @param s References our segmenter instance.
 * @param pSource Pointer to data source where we get ring items.
 * @param nEvents Number of events to send.
 * @param evtBufSize Size of each event buffer, must be big enough to hold a 
 *   single ring item.
 * @param rateGbps Send rate in Gbps (optional, default=1.0)
 * @param debug Show debugging output (optional, default=false)
 * @return EXIT_SUCCESS if successful, E2SAR error otherwise
 */
result<int>
sendEvents(Segmenter &s, DataSource* pSource, size_t nEvents,
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

    // // Start threads, open sockets. Start sending sync packets:
    
    auto open_rv = s.openAndStart();    
    if (open_rv.has_error()) {
	std::cerr << "Failed to start Segmenter: "
		  << open_rv.error().message() << std::endl;
        return open_rv;
    } else {
	std::cout << "Segmenter started OK\n";
    }	

    // Sleep to allow small number of frames to leave:
    
    boost::chrono::seconds duration(1);
    boost::this_thread::sleep_for(duration);

    /////////////////////////////////////////////////////////////////////////
    // Send loop
    //

    // Get the current time point:
	
    auto now = boost::chrono::high_resolution_clock::now();

    if (debug) {
	std::cout << "Starting send loop at: " << now << std::endl;
    }
    
    // // We run the send loop until we've sent all events or EOF:
    
    int remaining = nEvents;    
    while (1) {

	// Get a buffer, either from the queue or by allocating a new one:

	u_int8_t* evtBuf{nullptr};
	if (!evtBufQueue.pop(evtBuf)) {
	    evtBuf = static_cast<u_int8_t*>(evtBufPool->malloc());
	}

	// Get the ring item we'll pack into this buffer and send:
	
	std::unique_ptr<::ufmt::CRingItem> pItem(pSource->getItem());	
	if (!pItem.get()) { // End of source.
	    break;
	}
	
	// Extract information from the event, copy it into the event buffer,
	// and add it to the send queue. Event number is timestamp, data Id
	// is the source Id. In the case no body header is present, the
	// event timestamp is UINT64_MAX and the data Id is 0.

	EventNum_t evtNumber  = UINT64_MAX;
	u_int16_t  dataId     = 0;
	uint32_t   evtBufSize = pItem->size();
	u_int16_t  entropy    = 0;
	
	if (pItem->hasBodyHeader()) {
	    evtNumber = pItem->getEventTimestamp();
	    dataId = pItem->getSourceId();
	}
	
	memcpy(evtBuf, pItem->getItemPointer(), evtBufSize);
	
	if (debug) {
	    std::cout << "Sending event:" << std::endl;
	    std::cout << "\tevtNumber:  " << evtNumber << std::endl;
	    std::cout << "\tdataId:     " << dataId << std::endl;
	    std::cout << "\tevtBufSize: " << evtBufSize << std::endl;
	    std::cout << pItem->toString() << std::endl;
	}
	    
	auto sendq_rv = s.addToSendQueue(evtBuf, evtBufSize, evtNumber, dataId,
					 entropy, &freeBuffer, evtBuf);
	if (sendq_rv.has_error()) {
	    std::cout << sendq_rv.error().message() << std::endl;
	    continue;
	}
	
	if (nEvents != 0) {
	    remaining--;
	    if (remaining <= 0) {
		break;
	    }
	}

	// Wait to send the next event:
	
	auto until = now + boost::chrono::microseconds(interEventSleepUsec);
	if (now > until)
	{
	    return E2SARErrorInfo{E2SARErrorc::LogicError, 
		"Clock overrun, either event buffer length too short or "
		"requested sending rate too high"};
	}
	boost::this_thread::sleep_until(until);
	
    } // End of send loop

    // Free the backlog of unused buffers:
	
    u_int8_t* item{nullptr};
    while (evtBufQueue.pop(item)) {
	evtBufPool->free(item);
    }

    // Done sending events, report:
   
    auto stats = s.getSendStats();

    evtBufPool->purge_memory();
    std::cout << "Completed, " << stats.get<0>() << " frames sent, "
	      << stats.get<1>() << " errors" << std::endl;
    if (stats.get<1>() != 0)
    {
        std::cout << "Last error encountered: "
		  << strerror(stats.get<2>()) << std::endl;
    }
    
    return EXIT_SUCCESS;
}

/**
 * @brief Parse the URI of the source and based on the parse create the 
 * underlying connection. Create the correct concrete instance of DataSource 
 * given all that.
 * @param pFactory Pointer to the ring item factory to use.
 * @param strUrl   String URI of the connection.
 * @throw std::invalid_argument If a ringbuffer data source is requested.
 *   The unified format library is incorporated into the NSCLDAQ, but does
 *   not have NSCLDAQ support enabled as its installed first.
 * @return Dynamically allocated data source.
 * @note (ASC 11/19/24): Ringbuffer data sources not currently supported.
 *   If needed, create a pipe to read from stdin.
 */
DataSource*
makeDataSource(RingItemFactoryBase* pFactory, const std::string& strUrl)
{
    // Special case the url is just "-" then it's stdin, a file descriptor
    // data source:
    
    if (strUrl == "-") {
        return new FdDataSource(pFactory, STDIN_FILENO);   
    }
    
    // Parse the URI:    
    
    URL uri(strUrl);
    std::string protocol = uri.getProto();

    // URI could be for a ringbuffer or file:
    
    if ((protocol == "tcp") || (protocol == "ring")) {
	std::string msg(
	    "Ringbuffer support is not enabled for this version of "
	    "E2SAR send. To read data directly from a ringbuffer, "
	    "create a pipe to read from stdin: ringselector | send_simdata -"
	    );
	throw std::invalid_argument(msg);
    } else {
        std::string path = uri.getPath();
	// Need it to last past block:
        std::ifstream& in(*(new std::ifstream(path.c_str())));
	if (!in.good()) {
	    std::string msg("Failed to create input stream from ");
	    msg += path;
	    throw std::invalid_argument(msg);	    
	}
        return new StreamDataSource(pFactory, in);
    }
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
	signal(SIGINT, ctrlCHandler); // Ctrl-C signal.

	/////////////////////////////////////////////////////////////////////
	// Configure data source
	///

	int daqVersion = opts["nscldaq-version"].as<int>();
	FormatSelector::SupportedVersions version = mapVersion(daqVersion);
	auto& factory = FormatSelector::selectFactory(version);

	auto srcName = opts["source"].as<std::string>();
	std::unique_ptr<DataSource> pSource(makeDataSource(&factory, srcName));
    
	/////////////////////////////////////////////////////////////////////
	// Configure E2SAR
	///

	// Instance token for sending data:
    
	EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

	// Configure segmenter options:

	u_int32_t evtSourceId(0);
	u_int16_t dataId(0);
	
	size_t evtBufSize = opts["bufsize"].as<size_t>();
	size_t nEvents = 0;
	std::string uri_s("");
	bool debug = opts.count("debug");
	float rateGbps = opts["send-rate"].as<float>();
	std::string configFile(opts["config-file"].as<std::string>());
	std::string sendIP(opts["ip"].as<std::string>());

	// Override defaults if provided:

	if (opts.count("uri")) {
	    uri_s = opts["uri"].as<std::string>();
	}
	
	if (opts.count("num")) {
	    nEvents = opts["num"].as<size_t>();
	}
	
	auto flags = getSegmenterFlagsFromINI(configFile); // Sets IpV6 pref.
	auto uri = getURI(uri_s, tt, flags.dpV6);	    
    
	if (debug) {
	    std::cout << "Using E2SAR version: " << get_Version() << std::endl;
	    printSegmenterFlags(flags);
	    std::cout << "Using URI: " << uri.to_string() << std::endl;
	    std::cout << "Sending " << nEvents << " events" << std::endl;
	    std::cout << "Max buffer: " << evtBufSize << " bytes" << std::endl;
	}	

	/////////////////////////////////////////////////////////////////////
	// Instantiate and run Segmenter:
	///

	evtBufPool = new boost::pool<>{evtBufSize}; // For recycling buffers

	// Configure control plane (if used):

	if (flags.useCP) {
	    
	    // Note that when using CP the sender IP addr must be one of:
	    // IPv6: 2605:dd00:4000:82:130:1232:2709:0
	    // IPv4: 35.11.82.130
	    
	    senders.push_back(sendIP);

	    // Create the instance of the load balancer:
	    
	    lbmPtr = new LBManager(uri);

	    // Register senders:

	    std::cout << "Adding senders to LB:\n";
	    for (const auto& s: senders) {
		std::cout << s << " ";
	    }
	    std::cout << std::endl;

	    auto addSenders_rv = lbmPtr->addSenders(senders);

	    if (addSenders_rv.has_error()) {
		std::cerr << "Unable to add sender: "
			  << addSenders_rv.error().message()
			  << std::endl;
		std::cerr << "Exiting...\n";
		std::exit(EXIT_FAILURE);
	    }
	    
	    if (debug) {		
		auto uriStr
		    = lbmPtr->get_URI().to_string(EjfatURI::TokenType::session);
		auto addrStr = lbmPtr->get_AddrString();
		auto lbId = lbmPtr->get_URI().get_lbId();
		    
		std::cout << "Getting LB status:" << std::endl;
		std::cout << "\tContacting: " << uriStr
			  << " using address: " << addrStr
			  << std::endl;
		std::cout << "\tLB ID: " << lbId << std::endl;
	    }	    
	}	
	
	Segmenter seg(uri, dataId, evtSourceId, flags);
	segPtr = &seg;
	auto send_rv = sendEvents(seg, pSource.get(), nEvents,
				  evtBufSize, rateGbps, debug);
	if (send_rv.has_error()) {
	    std::cerr << "Segmenter encountered an error: "
		      << send_rv.error().message() << std::endl;
	    exit(EXIT_FAILURE);
	}

    }
    catch (std::invalid_argument& e) {
	std::cerr << "Failed to create data source: " << e.what() << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }
    catch (const E2SARException& e) {
	std::cerr << "Unable to create segmenter: "
		  << static_cast<std::string>(e) << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }
    
    return EXIT_SUCCESS;
}
