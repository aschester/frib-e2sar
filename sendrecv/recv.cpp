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

// Project headers:

#include "CDataSink.h"
#include "CFileDataSink.h"
#include "FribEjfatUtils.h"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;
using namespace ufmt;
using namespace frib_ejfat;

// Other global config:

bool threadsRunning(true);
u_int16_t reportThreadSleepMs{2000}; // 2 second maximum
Reassembler* reasPtr{nullptr};

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
	("sink,S",
	 po::value<std::string>()->required(),
	 "path to output file data sink (*not* a URI)")
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
    
    auto hostrv = NetUtil::getHostName();
    if (hostrv.has_error()) 
    {
        return E2SARErrorInfo{hostrv.error().code(), hostrv.error().message()};
    }
    
    auto regrv = r->registerWorker(hostrv.value());
    if (regrv.has_error())
    {
	std::string msg("Unable to register worker node: ");
	msg += regrv.error().message();
        return E2SARErrorInfo{E2SARErrorc::RPCError, msg};
    }

    boost::this_thread::sleep_for(boost::chrono::seconds(1));

    // @note If we switch the order of registerWorker and openAndStart
    // you get into a race condition where the sendState thread starts and
    // tries to send queue updates, however the session token is not yet
    // available...
    
    auto oasrv = r->openAndStart();
    if (oasrv.has_error()) {
        return oasrv;
    }
    
    return EXIT_SUCCESS;
}

/**
 * @brief Receive and reassemble events.
 * @param r Pointer to our Reassembler instance.
 * @param pSink Pointer to the (for now always a file) data sink we write to.
 * @param factory Factory for creating formatted ring items.
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
recvEvents(Reassembler* r, Reassembler::ReassemblerFlags& flags,
	   CDataSink* pSink, RingItemFactoryBase& factory, int durationSec,
	   FormatSelector::SupportedVersions version, bool debug=false)
{    
    // Received event information and receiver config. We recycle the buffer
    // with blocking calls to receive data. The extent of good data for a
    // particular event is defined by evtBufSize.
    
    u_int8_t*  evtBuf{nullptr}; // Event buffer
    size_t     evtBufSize;      // Event buffer size in bytes
    EventNum_t evtNum;          // Event number (typically timestamp)
    u_int16_t  dataId;          // Data Id (source Id or other)
    u_int64_t  waitMs = 1000;   // Wait time in milliseconds
    
    auto now = boost::chrono::steady_clock::now();

    /////////////////////////////////////////////////////////////////////////
    // Receive loop
    ///
    
    while(threadsRunning)
    {	
	// Blocking receive. Use getEvent() for non-blocking:
	
	auto rv = r->recvEvent(&evtBuf, &evtBufSize, &evtNum,
				   &dataId, waitMs);
	//auto rv = r->getEvent(&evtBuf, &evtBufSize, &evtNum, &dataId);
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
	// The event buffer is a complete ring item. All the information we
	// need to re-form the ring item on this end can be parsed from the
	// ring item's header and body header. We extract the body size based
	// on the remaining size after the header sizes are subtracted and
	// then do byte-by-byte copy of the buffer into the ring item body.
	///

	u_int8_t* p = evtBuf;         // Pointer to first byte of evtBuf.
	size_t bodySize = evtBufSize; // In bytes.
	
	auto pHdr = reinterpret_cast<RingItemHeader*>(p);
	p += sizeof(RingItemHeader);
	bodySize -= sizeof(RingItemHeader);

	// Body headers exist starting with NSCLDAQ 11. Note that this section
	// of code breaks compatibility with v10:
	
	auto pBodyHdr = reinterpret_cast<BodyHeader*>(p);
	size_t bodyHdrSize = pBodyHdr->s_size;
	// v11 body header size is not self-inclusive if not present:	
	if (bodyHdrSize == 0 && version == FormatSelector::v11) {
	    bodyHdrSize = sizeof(uint32_t); // Inclusive size
	}	
	p += bodyHdrSize;
	bodySize -= bodyHdrSize; // Whatever is left is the payload.

	std::unique_ptr<CRingItem> pItem(
	    factory.makeRingItem(pHdr->s_type, bodySize)
	    );
	if (bodyHdrSize > sizeof(uint32_t)) {
	    pItem->setBodyHeader(pBodyHdr->s_timestamp,
				 pBodyHdr->s_sourceId,
				 pBodyHdr->s_barrier);
	}	
	u_int8_t* pBody = reinterpret_cast<u_int8_t*>(pItem->getBodyCursor());
	memcpy(pBody, p, bodySize);
	pBody += bodySize;	    
	pItem->setBodyCursor(pBody);
	pItem->updateSize();
		
	if (debug) {
	    std::cout << "Receive event:" << std::endl;
	    std::cout << "\tevtNumber:  " << evtNum << std::endl;
	    std::cout << "\tdataId:     " << dataId << std::endl;
	    std::cout << "\tevtBufSize: " << evtBufSize << std::endl;
	    std::cout << pItem->toString() << std::endl;
	}

	pSink->putItem(*pItem.get());
	
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
recvStatsThread(Reassembler *r)
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

        // std::cout << "\tEvents lost so far: ";
        // for(auto evt: lostEvents)
        // {
        //     std::cout << "<" << evt.first << ":" << evt.second << "> ";
        // }
        // std::cout << std::endl;

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

	int daqVersion = opts["nscldaq-version"].as<int>();
	FormatSelector::SupportedVersions version = mapVersion(daqVersion);
	auto& factory = FormatSelector::selectFactory(version);
	
	auto name = opts["sink"].as<std::string>();
	std::unique_ptr<CFileDataSink> pSink(new CFileDataSink(name));
	
	/////////////////////////////////////////////////////////////////////
	// Configure E2SAR
	///

	EjfatURI::TokenType tt{EjfatURI::TokenType::instance};
    
	auto preferV6 = opts["preferV6"].as<bool>();
	auto ip_s = opts["ip"].as<std::string>();
	auto port = opts["port"].as<u_int16_t>();
	int durationSec = opts["duration"].as<int>();
	size_t numThreads = opts["threads"].as<size_t>(); // Reassembler
	size_t deqThreads = opts["deq"].as<size_t>();     // Dequeue/read
	std::string configFile(opts["config-file"].as<std::string>());
	bool debug = opts.count("debug");

	std::string uri_s("");
	if (opts.count("uri")) {
	    uri_s = opts["uri"].as<std::string>();
	}
	
	auto flags = getReassemblerFlagsFromINI(configFile);
	auto uri = getURI(uri_s, tt, preferV6);

	if (debug) {
	    std::cout << "Using E2SAR version: " << get_Version() << std::endl;
	    printReassemblerFlags(flags);
	    std::cout << "Using URI: " << uri.to_string() << std::endl;
	}
    
	/////////////////////////////////////////////////////////////////////
	// Instantiate and run Segmenter:
	///
	
	ip::address ip = ip::make_address(ip_s);
	reasPtr = new Reassembler(uri, ip, port, numThreads, flags);
	
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
	    boost::thread syncT(recvEvents, reasPtr, flags, pSink.get(),
				std::ref(factory), durationSec, version,
				debug);
	    threads.push_back(std::move(syncT)); // Transfer, dont copy!
	}

	for (auto& t : threads) // Must be a reference.
	    t.join();
	
	shutdown(); // Shutdown if duration is not infinite.
	       
    } catch (E2SARException &e) {
	std::cerr << "Unable to create reassembler: "
		  << static_cast<std::string>(e) << std::endl;
	exit(EXIT_FAILURE);
    }
    catch (CException& e) {
	std::cerr << "Failed to create data sink: "
		  << e.ReasonText() << std::endl;
	return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
