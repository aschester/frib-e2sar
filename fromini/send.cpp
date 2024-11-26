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
 * @brief Simple send with no load balancer.
 */

#include <iostream>
#include <cstddef>
#include <string>

#include <boost/program_options.hpp>

#include <e2sar.hpp>
#include <e2sarDPSegmenter.hpp>

namespace po = boost::program_options;
using namespace e2sar;

// Prepare a pool. To avoid locking the pool we use the return queue.
// The boost::lockfree::queue is a thread-safe, lock- and mutex-free queue
// that instead require atomic operations:

boost::pool<> *evtBufPool;
boost::lockfree::queue<u_int8_t*> returnBufferQueue{10000};

// Event payload:

uint16_t evtPldStart = 0x1234;
uint16_t evtPldEnd = 0xabcd;

// Other global config:

bool threadsRunning(true);
Segmenter* segPtr{nullptr};
LBManager* lbmPtr{nullptr};       // nullptr if CP is not enabled
std::vector<std::string> senders; // Empty if CP is not enabled

void ctrlCHandler(int sig) 
{
    std::cout << "Stopping threads" << std::endl;

    if (segPtr != nullptr) {
        if (lbmPtr != nullptr) {
            std::cout << "Removing senders: ";
            for (auto s: senders)
                std::cout << s << " ";
            std::cout << std::endl;
            auto rmres = lbmPtr->removeSenders(senders);
            if (rmres.has_error()) 
                std::cerr << "Unable to remove sender from list on exit: "
			  << rmres.error().message() << std::endl;
        }
        segPtr->stopThreads();
    }

    threadsRunning = false;
    // Instead of join on the main thread??
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);

    // Re-raise the signal and invoke default behavior:
    
    signal(sig, SIG_DFL);
    raise(sig);    
}

po::variables_map
getOpts(int ac, char* av[])
{
    // Configure and parse command-line options:
     
    po::options_description od("Send command-line options");
    auto opts = od.add_options()
	("help,h", "show command help")
	("config-file,c",
	 po::value<std::string>()->default_value("./segmenter_config.ini"),
	 "path to configuration file")
	("uri,u",
	 po::value<std::string>(),
	 "URI from the command line to override EJFAT_URI envvar")
	("num,n",
	 po::value<size_t>()->default_value(10),
	 "number of events to send")
	("bufsize,s",
	 po::value<size_t>()->default_value(1024*1024),
	 "event buffer size in bytes")
	("debug", "enable debugging output")
	;
    po::variables_map vm; // Command line options stored here.
    po::store(po::parse_command_line(ac, av, od), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << od << std::endl;
        exit(EXIT_SUCCESS);
    }

    return vm;
}

EjfatURI
getURI(const po::variables_map& opts, const EjfatURI::TokenType& tt,
       const bool preferV6=false)
{
    auto uri_rv = (
	opts.count("uri") ?
	EjfatURI::getFromString(opts["uri"].as<std::string>(), tt, preferV6)
	: EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6)
	);	
    if (uri_rv.has_error())
    {
	std::cerr << "Error in parsing URI from command-line: "s
	    + uri_rv.error().message() << std::endl;
	exit(EXIT_FAILURE);
    }
    
    return uri_rv.value();
}

Segmenter::SegmenterFlags
getFlags(const po::variables_map& opts)
{
    auto flags_rv = Segmenter::SegmenterFlags::getFromINI(
	opts["config-file"].as<std::string>()
	);
    if (flags_rv.has_error()) {
	std::cerr << "Error reading configuration file: "s
	    + flags_rv.error().message() << std::endl;
	exit(EXIT_FAILURE);
    }

    auto flags = flags_rv.value();
    
    // Print out some info about the flags:
    
    std::cout << "Control plane                "
	      << (flags.useCP ? "ON" : "OFF") << std::endl;
    std::cout << "Event rate reporting in Sync "
	      << (flags.zeroRate ? "OFF" : "ON") << std::endl;
    std::cout << "Using usecs as event numbers "
	      << (flags.usecAsEventNum ? "ON" : "OFF") << std::endl;
    std::cout << "Number of send sockets:      "
	      << flags.numSendSockets << std::endl;
    std::cout << (flags.useCP ?
		  "*** Make sure the LB has been reserved and the URI "
		  "reflects the reserved instance information."
		  : "*** Make sure the URI reflects proper data "
		  "address, other parts are ignored.") << std::endl;
    
    return flags;
}

void
printFlags(const Segmenter::SegmenterFlags& flags)
{
    std::cout << "Segmenter flags:\n";
    std::cout << "\tdpV6\t\t" << flags.dpV6 << std::endl;
    std::cout << "\tzeroCopy\t" << flags.zeroCopy << std::endl;
    std::cout << "\tconnectedSocket\t" << flags.connectedSocket << std::endl;
    std::cout << "\tuseCP\t\t" << flags.useCP << std::endl;
    std::cout << "\tzeroRate\t" << flags.zeroRate << std::endl;
    std::cout << "\tusecAsEventNum\t" << flags.usecAsEventNum << std::endl;
    std::cout << "\tsyncPeriodMs\t" << flags.syncPeriodMs << std::endl;
    std::cout << "\tsyncPeriods\t" << flags.syncPeriods << std::endl;
    std::cout << "\tmtu\t\t" << flags.mtu << " (bytes)" << std::endl;
    std::cout << "\tnumSendSockets\t" << flags.numSendSockets << std::endl;
    std::cout << "\tsndSockBufSize\t" << flags.sndSocketBufSize << " (bytes)"
	      << std::endl;
}

void freeBuffer(boost::any a) 
{
    auto p = boost::any_cast<u_int8_t*>(a);
    returnBufferQueue.push(p);
}

result<int> sendEvents(Segmenter &s, EventNum_t startEvtNum,
		       size_t numEvts, size_t evtBufSize,
		       float rateGbps=1.0, bool debug=false)
{
    // Convert bit rate to event rate:
    
    float eventRate{rateGbps*1000000000/(evtBufSize*8)};
    u_int64_t interEventSleepUsec{
	static_cast<u_int64_t>(evtBufSize*8/(rateGbps * 1000))
    };

    std::cout.imbue(std::locale(""));
    std::cout << "Sending bit rate is " << rateGbps << " Gbps" << std::endl;
    std::cout << "Event size is " << evtBufSize << " bytes or "
	      << evtBufSize*8 << " bits" << std::endl;
    std::cout << "Event rate is " << eventRate << " Hz" << std::endl;
    std::cout << "Inter-event sleep time is " << interEventSleepUsec
	      << " microseconds" << std::endl;
    std::cout << "Sending " << numEvts << " event buffers" << std::endl;
    std::cout << "Using MTU " << s.getMTU() << std::endl;

    // Our payload needs to be big enough to hold the start and end, at least:
    
    if (s.getMaxPldLen() < sizeof(evtPldStart) + sizeof(evtPldEnd))
        return E2SARErrorInfo{
	    E2SARErrorc::LogicError, "MTU is too short to send needed payload"
	};
    
    // Start threads, open sockets. Start sending sync packets:
    
    auto open_rv = s.openAndStart();
    if (open_rv.has_error()) {
        return open_rv;
    } else {
	// Note we have to get the _value_ of the return object:
	std::cout << "Started segmenter with result " << open_rv.value()
		  << std::endl;
    }

    // Initialize a pool of memory buffers we will be sending:
    
    evtBufPool = new boost::pool<>{evtBufSize};

    // Sleep to allow small number of frames to leave:
    
    boost::chrono::seconds duration(1);
    boost::this_thread::sleep_for(duration);

    /////////////////////////////////////////////////////////////////////////
    // Send loop
    //
    
    for(size_t evt = 0; evt < numEvts; evt++)
    {
        // Get the current time point:
	
        auto now = boost::chrono::high_resolution_clock::now();

        // Send the event:

	// Note: boost::pool malloc returns new'd array. Deleted in recv?
	
        auto evtBuf = static_cast<u_int8_t*>(evtBufPool->malloc());
	
        // Fill in the first part of the buffer with something meaningful
	// and also the end. The rest of it is whatever random data:
	
        memcpy(evtBuf, &evtPldStart, sizeof(evtPldStart));
	memcpy(evtBuf + evtBufSize - sizeof(evtPldEnd),
	       &evtPldEnd, sizeof(evtPldEnd));

	if (debug) {
	    for (int i = 0; i < evtBufSize; i++) {
		std::cout << std::hex << (int)evtBuf[i] << " ";
		if (i > 0 && i%8 == 0) std::cout << std::endl;
	    }
	    std::cout << std::dec << std::endl;
	}

        // Put on queue with a callback to free this buffer. We probably want
	// to fill in the data ID with something more meaningful here.

	// Note: we override the default data ID here
	
	auto sendRes = s.addToSendQueue(evtBuf, evtBufSize, startEvtNum,
					0, 0, &freeBuffer, evtBuf);

	// Wait to send the next event:
	
	auto until = now + boost::chrono::microseconds(interEventSleepUsec);
	if (now > until)
	{
	    return E2SARErrorInfo{E2SARErrorc::LogicError, 
		"Clock overrun, either event buffer length too short or "
		"requested sending rate too high"};
	}
	
	// Free the backlog of empty buffers:
	
	u_int8_t *item{nullptr};
	while (returnBufferQueue.pop(item)) {
	    evtBufPool->free(item);
	}
	boost::this_thread::sleep_until(until);
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
    
    return 0;
}

int
main(int argc, char* argv[])
{    
    auto opts = getOpts(argc, argv); // Command-line options.
    signal(SIGINT, ctrlCHandler); // Ctrl-C signal:
    
    /////////////////////////////////////////////////////////////////////////
    // Setup
    ///
    
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

    // Configure segmenter options:

    u_int32_t evtSourceId(0);
    u_int16_t dataId(0);
    EventNum_t startEvtNum{0};
    size_t numEvts = opts["num"].as<size_t>();
    size_t evtBufSize = opts["bufsize"].as<size_t>();
    auto flags = getFlags(opts); // Call first, set IPv preference
    auto uri = getURI(opts, tt, flags.dpV6);
    float rateGbps = 1.0;
    bool debug = opts.count("debug");
    
    if (debug) {
	std::cout << "Using E2SAR version: " << get_Version() << std::endl;
       	printFlags(flags);
	std::cout << "Using URI: " << uri.to_string() << std::endl;
	std::cout << "Sending " << numEvts << " events" << std::endl;
	std::cout << "Evtbuf size: " << evtBufSize << " bytes" << std::endl;
    }

    // Instantiate and run:

    try {
	Segmenter seg(uri, dataId, evtSourceId, flags);
	segPtr = &seg;
	auto send_rv = sendEvents(seg, startEvtNum, numEvts, evtBufSize,
				  rateGbps, debug);
	if (send_rv.has_error()) {
	    std::cerr << "Segmenter encountered an error: "
		      << send_rv.error().message() << std::endl;
	    exit(EXIT_FAILURE);
	}
    } catch (const E2SARException& e) {
	std::cerr << "Unable to create segmenter: "
		  << static_cast<std::string>(e) << std::endl;
	exit(EXIT_FAILURE);
    }
    
    return EXIT_SUCCESS;
}
