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
 * @file recv.cpp
 * @brief Simple receive of NSCLDAQ data with or without load balancer.
 */

#include <iostream>
#include <cstddef>
#include <string>
#include <csignal>

#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <e2sar.hpp>
#include <e2sarDPReassembler.hpp>

// Unified format library:

#include <DataFormat.h>
#include <RingItemFactoryBase.h>
#include <CRingItem.h>

// Other NSCLDAQ headers:

#include <Exception.h>
#include <URL.h>

// Project headers:

#include "DataSink.h"
#include "FileDataSink.h"
#include "RingDataSink.h"
#include "MapVersion.h"

#include <FribEjfatUtils.h>

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;
using namespace ufmt;
using namespace frib_ejfat;

// Other global config:

bool threadsRunning(true);
u_int16_t reportThreadSleepMs{2000}; // 2 second maximum
Reassembler* reasPtr{nullptr};
const uint32_t MAX_BYTES = 2.0*1024*1024*1024; // ~2 GB

/**
 * @brief Shutdown the receiver. Deregister workers. Stop threads.
 */
void
shutdown() {
    std::cout << "Stopping threads" << std::endl;
    threadsRunning = false;
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);
    
    if (reasPtr != nullptr)
    {
        std::cout << "Deregistering worker" << std::endl;
        auto rv = reasPtr->deregisterWorker();
        if (rv.has_error()) {
            std::cerr << "Unable to deregister worker on exit: "
		      << rv.error().message() << std::endl;
	}
        reasPtr->stopThreads();
	delete reasPtr;
    }

    boost::this_thread::sleep_for(duration);
}

/**
 * @brief Handle interrupt and shutdown.
 * @param sig Interrupt signal.
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
 * @param ac Argument count.
 * @param av Argument vector.
 * @return Variables map.
 */
po::variables_map
getOpts(int ac, char* av[])
{    
    po::options_description od("Receive command-line options");
    od.add_options()
	("help,h", "show command help")
	("config-file,c",
	 po::value<std::string>()->default_value("./reassembler_config.ini"),
	 "path to configuration file")
	("uri,u",
	 po::value<std::string>(),
	 "URI from the command line to override EJFAT_URI envvar")
	("ip",
	 po::value<std::string>()->default_value("35.11.82.130"),
	 "IP address (IPv4 or IPv6) on which receiver listens.")
	("port",
	 po::value<u_int16_t>()->default_value(23457),
	 "starting UDP port number on which receiver listens.")
	("output-path,o",
	 po::value<std::string>()->required(),
	 "Top-level directory where output is written")
	("run-number,r",
	 po::value<size_t>()->required(),
	 "Run number")
	("threads,t",
	 po::value<size_t>()->default_value(1),
	 "number of reassembler threads")
	("deq",
	 po::value<size_t>()->default_value(1),
	 "number of dequeue/read threads in receiver")
	("duration,d",
	 po::value<int>()->default_value(0),
	 "seconds to run the receiver, 0 for inifinite")
	("preferV6",
	 po::value<bool>()->default_value(false),
	 "prefer IPv6 over IPv4")
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
 * @brief Make a (file) sink URI from the path and run information.
 * @param outPath Toplevel output directory.
 * @param runNumber Current run number.
 * @param currentSegment Current segment we're writing to.
 * @return Full file sink name.
 */
std::string
makeSinkUri(std::string outPath, size_t runNumber, size_t currentSegment)
{
    std::string uri("file://");
    uri += outPath;
    char path[1024];
    sprintf(path, "run-%04d-%02d.evt", runNumber, currentSegment);
    uri += path;

    return uri;
}

/**
 * @brief Create and return dynamically created sink based on the URI proto.
 * @param strUrl References the URI string for the sink (ringbuffer or file).
 * @throw std::runtime_error If the URI protocol is not known.
 * @return Pointer to the dynamically created sink.
 * @todo (ASC 3/3/25): Exception if ringbuffer is not localhost.
 * @todo (ASC 3/3/25): Catch and handle all the ways constructors can fail,
 *   make sure this exception is handled in main.
 */
DataSink*
makeDataSink(const std::string& strUrl)
{
    URL url(strUrl);
    std::string protocol = url.getProto();
    std::string path = url.getPath();
    if (protocol == "tcp" || protocol == "ring") {
	return new RingDataSink(path);
    } else if (protocol == "file") {
	return new FileDataSink(path);
    } else {
	std::string msg("unknown proto for sink ");
	msg += protocol;
	throw std::runtime_error(msg);
    }
}

/**
 * @brief Return the size of the item.
 * @param pData Pointer to a ring item.
 * @return Number of bytes in that item.
 */
size_t
itemSize(void* pData)
{
    return static_cast<RingItemHeader*>(pData)->s_size;
}

/**
 * @breif Get pointer to beginning of next item.
 * @param pData Pointer to data block.
 * @return void* Pointer to the next item in the block.
 */
void*
nextItem(void* pData)
{
    size_t n = itemSize(pData);
    uint8_t* p = static_cast<uint8_t*>(pData);
    p += n;
    
    return p;
}

/**
 * @brief Count the number of items in a block of data.
 * @param pData Pointer to the data.
 * @param nBytes Number of bytes in the block.
 * @return Number of items in the block.
 */
size_t
countRingItems(void* pData, size_t nBytes)
{
    size_t result(0);
    while (nBytes) {
        result++;
        nBytes -= itemSize(pData);
        pData   = nextItem(pData);
    }
    
    return result;
}

/**
 * @brief Register workers and start the receiver threads.
 * @param r Pointer to Reassembler instance.
 * @return EXIT_SUCCESS if successful, E2SAR error otherwise.
 * @note This function must be called prior to the receive loop to ensure that
 * workers are registered only once.
 */
result<int>
prepareToReceive(Reassembler* r)
{           
    std::cout << "Receiving on ports " << r->get_recvPorts().first
	      << ":" << r->get_recvPorts().second << std::endl;

    // Worker registration is NOP if not using control plane:
    
    auto rv_hname = NetUtil::getHostName();
    if (rv_hname.has_error()) 
    {
        return E2SARErrorInfo{rv_hname.error().code(),
	    rv_hname.error().message()};
    }
    
    auto rv_reg = r->registerWorker(rv_hname.value());
    if (rv_reg.has_error())
    {
	std::string msg("Unable to register worker node: ");
	msg += rv_reg.error().message();
        return E2SARErrorInfo{E2SARErrorc::RPCError, msg};
    }

    boost::this_thread::sleep_for(boost::chrono::seconds(1));

    // @note If we switch the order of registerWorker and openAndStart
    // you get into a race condition where the sendState thread starts and
    // tries to send queue updates, however the session token is not yet
    // available...
    
    auto rv_oas = r->openAndStart();
    if (rv_oas.has_error()) {
        return rv_oas;
    }
    
    return EXIT_SUCCESS;
}

/**
 * @brief Receive and reassemble events.
 * @param r Pointer to our Reassembler instance.
 * @param factory Factory for creating formatted ring items.
 * @param version NSCLDAQ format version (for parsing v11 vs. v12 headers).
 * @param durationSec Listening duration; if 0, listen forever.
 * @param debug Enable debugging output.
 * @return EXIT_SUCCESS if successful, E2SAR error otherwise.
 * @note (ASC 11/26/24): Not compatible with NSCLDAQ 10 at the moment. 
 *   The data unpacking from an arbitrary buffer containing a complete ring 
 *   item is complicated by the fact the body headers do not exist in v10. 
 *   Easiest approach is to leave as-is and assume nobody will ever try this 
 *   with v10 data. A more complete solution would entail switching the buffer
 *   unpacking method based on the DAQ version provided by the user.
 */
result<int>
recvEvents(Reassembler* r, RingItemFactoryBase& factory,
	   FormatSelector::SupportedVersions version,
	   std::string outPath, size_t runNumber, int durationSec,
	   bool debug=false)
{
    // Create the initial data sink, which we really expect to be a file:

    size_t currentSegment = 0;
    std::string sinkUri = makeSinkUri(outPath, runNumber, currentSegment);
    std::unique_ptr<DataSink> pSink(makeDataSink(sinkUri));
    
    // Received event information and receiver config. We recycle the buffer
    // with blocking calls to receive data. The extent of good data for a
    // particular event is defined by evtBufSize.
    
    u_int8_t*  evtBuf{nullptr}; // Event buffer for data reads
    size_t     evtBufSize;      // Event buffer size in bytes
    EventNum_t evtNum;          // Event number (typically timestamp)
    u_int16_t  dataId;          // Data Id (source Id or other)
    u_int64_t  waitMs = 1000;   // Wait time in milliseconds
    
    auto now = boost::chrono::steady_clock::now();

    /////////////////////////////////////////////////////////////////////////
    // Receive loop
    ///

    size_t currentBytes = 0;
    size_t totalBytes = 0;
    
    while(threadsRunning)
    {	
	// recvEvent is blocking receive. Use getEvent() for non-blocking:
	
	auto rv = r->recvEvent(&evtBuf, &evtBufSize, &evtNum,
			       &dataId, waitMs);
	// auto rv = r->getEvent(&evtBuf, &evtBufSize, &evtNum, &dataId);
	
        auto next = boost::chrono::steady_clock::now();

	// If duration is set stop listening after that time and exit.
	// Note that we handle shutdown in main after the read thread(s)
	// have exited.
	
        if ((durationSec != 0)
	    && ((next - now) > boost::chrono::seconds(durationSec)))
            break;
	
        if (rv.has_error())
            return rv;
	
        if (rv.value() == -1) // Queue is empty
            continue;	

	/////////////////////////////////////////////////////////////////////
	// Data post-processing:
	//
	// The event buffer contains complete ring items. All the information
	// needed to re-form the ring items on this end can be parsed from the
	// ring item headers and body headers. We extract the body size based
	// on the remaining size after the header sizes are subtracted and
	// then do byte-by-byte copy of the buffer into the ring item body.
	///

	u_int8_t* p = evtBuf; // Pointer to first byte
	size_t nItems = countRingItems(evtBuf, evtBufSize);
	std::vector<iovec> iovs(nItems);
	
	// Unpack buffer into iovecs:
	for (size_t i = 0; i < nItems; i++) {
	    iovs[i].iov_base = p;
	    iovs[i].iov_len = itemSize(p);
	    p = static_cast<u_int8_t*>(nextItem(p));
	}

	pSink->putItemsV(iovs.data(), iovs.size());

	currentBytes += evtBufSize;
	
	if (debug) {
	    std::cout << "Receive event:" << std::endl;
	    std::cout << "\tevtNumber:  " << evtNum << std::endl;
	    std::cout << "\tdataId:     " << dataId << std::endl;
	    std::cout << "\tevtBufSize: " << evtBufSize << std::endl;
	}
		
	if (currentBytes > MAX_BYTES) {
	    // Track how much we've written:
	    totalBytes += currentBytes;
	    currentBytes = 0;
	    // Segment output:
	    currentSegment++;
	    sinkUri = makeSinkUri(outPath, runNumber, currentSegment);
	    pSink.reset(makeDataSink(sinkUri));
	}
	
	delete evtBuf;
	evtBuf = nullptr;
    }
        
    return 0;
}

/**
 * @brief Monitor for event reassembly.
 * @param r Pointer to Reassembler.
 */
void
recvStatsThread(Reassembler* r)
{
    std::vector<std::pair<EventNum_t, u_int16_t>> lostEvents;

    while(threadsRunning)
    {
        auto now = boost::chrono::high_resolution_clock::now();
        auto stats = r->getStats();

        while(true)
        {
            auto rv = r->get_LostEvent();
            if (rv.has_error())
                break;
            lostEvents.push_back(rv.value());
        }

        std::cout << "Stats:" << std::endl;	
	std::cout << "\tCurrent time: " << pt::second_clock::local_time()
		  << std::endl;
        std::cout << "\tEvents Received: " << stats.get<1>() << std::endl;
        std::cout << "\tEvents Lost: " << stats.get<0>() << std::endl;
        std::cout << "\tData Errors: " << stats.get<4>() << std::endl;
        if (stats.get<4>() > 0) {
            std::cout << "\tLast Data Error: "
		      << strerror(stats.get<2>()) << std::endl;
	    std::cout << "\tgRPC Errors: " << stats.get<3>() << std::endl;
	}
        if (stats.get<5>() != E2SARErrorc::NoError) {
            std::cout << "\tLast E2SARError code: "
		      << stats.get<5>() << std::endl;
	}

        std::cout << "\tEvents lost so far: ";
        for(auto evt: lostEvents)
        {
            std::cout << "<" << evt.first << ":" << evt.second << "> ";
        }
        std::cout << std::endl;

        auto until = now + boost::chrono::milliseconds(reportThreadSleepMs);
        boost::this_thread::sleep_until(until);
    }
}

/**
 * @brief Receive main. Create a data sink and Reassembler; listen for data and
 * write it to the sink.
 */
int
main(int argc, char* argv[])
{    
    auto opts = getOpts(argc, argv); // Command-line options.
    signal(SIGINT, ctrlCHandler);    // Ctrl-C signal:

    try {

	/////////////////////////////////////////////////////////////////////
	// Configure data sink
	///
	
	auto daqVersion = opts["nscldaq-version"].as<int>();
	auto version = mapVersion(daqVersion);
	auto& factory = FormatSelector::selectFactory(version);

	auto outPath = opts["output-path"].as<std::string>();
	auto runNumber = opts["run-number"].as<size_t>();
	
	/////////////////////////////////////////////////////////////////////
	// Configure E2SAR
	///

	EjfatURI::TokenType tt{EjfatURI::TokenType::instance};
    
	auto preferV6 = opts["preferV6"].as<bool>();
	auto ip_s = opts["ip"].as<std::string>();
	auto port = opts["port"].as<u_int16_t>();
	auto durationSec = opts["duration"].as<int>();
	auto numThreads = opts["threads"].as<size_t>(); // Reassembler
	auto deqThreads = opts["deq"].as<size_t>();     // Dequeue/read
	auto configFile(opts["config-file"].as<std::string>());
	bool debug = opts.count("debug");

	std::string ejfatUri_s("");
	if (opts.count("uri")) {
	    ejfatUri_s = opts["uri"].as<std::string>();
	}
	
	auto flags = getReassemblerFlagsFromINI(configFile);
	auto ejfatUri = getURI(ejfatUri_s, tt, preferV6);

	if (debug) {
	    std::cout << "Using E2SAR version: " << get_Version() << std::endl;
	    printReassemblerFlags(flags);
	    std::cout << "Using URI: " << ejfatUri.to_string() << std::endl;
	}
    
	/////////////////////////////////////////////////////////////////////
	// Instantiate and run Segmenter:
	///
	
	ip::address ip = ip::make_address(ip_s);
	reasPtr = new Reassembler(ejfatUri, ip, port, numThreads, flags);
	
	boost::thread statsThread(&recvStatsThread, reasPtr);

	auto rv = prepareToReceive(reasPtr);
	if (rv.has_error()) {
	    std::cerr << "Reassembler encountered an error: "
		      << rv.error().message() << std::endl;
	    shutdown();
	    exit(EXIT_FAILURE);
	}

	std::vector<boost::thread> threads;
	for(size_t i = 0; i < deqThreads; i++)
	{
	    boost::thread syncT(recvEvents, reasPtr, std::ref(factory),
				version, outPath, runNumber, durationSec,
				debug);
	    threads.push_back(std::move(syncT)); // Transfer, dont copy!
	}

	for (auto& t : threads) { // Must be a reference.
	    t.join();
	}
	
    }
    catch (E2SARException &e) {
	auto msg = static_cast<std::string>(e);
	std::cerr << "E2SAR exception: " << msg << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }
    catch (std::string& e) {
	std::cerr <<  "std::string exception: " << e << std::endl;
	shutdown();
	exit(EXIT_FAILURE);
    }
    catch (...) {
	std::cerr << "Caught unexpected exception type, exiting" << std::endl;
	shutdown();
	return EXIT_FAILURE;
    }

    shutdown();  // Shutdown for finite duration run.
    
    return EXIT_SUCCESS;
}
