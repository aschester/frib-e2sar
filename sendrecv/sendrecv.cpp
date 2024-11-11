/** 
 * @file sendrecv.cpp
 * @brief Simple send-receive test with no load balancer.
 */

#include <iostream>
#include <cstddef>
#include <string>

#include <boost/program_options.hpp>

#include <e2sar.hpp>

namespace po = boost::program_options;
using namespace e2sar;

// Prepare a pool. To avoid locking the pool we use the return queue.
// The boost::lockfree::queue is a thread-safe, lock- and mutex-free queue
// that instead require atomic operations:

boost::pool<> *evtBufferPool;
boost::lockfree::queue<u_int8_t*> returnBufferQueue{10000};

// Event payload:

auto eventPldStart = "This is a start of event payload"s;
auto eventPldEnd = "...the end"s;

// Our E2SAR app:

bool threadsRunning(true);
u_int16_t reportThreadSleepMs{1000}; // 1 second maximum
Segmenter* segPtr{nullptr};
Reassembler* reasPtr{nullptr};
LBManager* lbmPtr{nullptr};          // nullptr if !withCP
std::vector<std::string> senders;    // Empty if !withCP

/**
 * @brief Cleanup and exit on Ctrl-C.
 */
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
    
    if (reasPtr != nullptr)
    {
        std::cout << "Deregistering worker" << std::endl;
        auto deregres = reasPtr->deregisterWorker();
        if (deregres.has_error()) 
            std::cerr << "Unable to deregister worker on exit: "
		      << deregres.error().message() << std::endl;
        reasPtr->stopThreads();
    }

    threadsRunning = false;
    // Instead of join on the main thread??
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);

    // Re-raise the signal and invoke default behavior:
    
    signal(sig, SIG_DFL);
    raise(sig);    
}


/**
 * @brief Ensure 'opt1' and 'opt2' are not specified simultaneously.
 * @throw std::logic_error If conflicting options are specified.
 */
void conflicting_options(const po::variables_map &vm,
			 const std::string &opt1, const std::string &opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted()
	&& vm.count(opt2) && !vm[opt2].defaulted()) {
	std::string msg("Conflicting options '");
	msg += opt1 + "' and '" + opt2 + "'.";
        throw std::logic_error(msg);
    }
}

/**
 * @brief Check that if 'for_what' is specified, then 'required_option' is 
 * specified too. 
 * @throw std::logic_error If required options are not specified.
 */
void option_dependency(const po::variables_map &vm,
                       const std::string &for_what,
		       const std::string &required_option)
{
    if (vm.count(for_what) && !vm[for_what].defaulted())
        if (vm.count(required_option) == 0 || vm[required_option].defaulted())
            throw std::logic_error(std::string("Option '")
				   + for_what + "' requires option '"
				   + required_option + "'.");
}

/**
 * @brief Callback function to free a buffer and return it to the queue.
 * @param a Boost object of some general type.
 * @details
 * Cast to u_int8_t* and return to the pool.
 */
void freeBuffer(boost::any a) 
{
    // What are the contents of 'a'? Does it matter? -- ASC 11/1/24
    auto p = boost::any_cast<u_int8_t*>(a);
    returnBufferQueue.push(p);
}

result<int> sendEvents(Segmenter &s, EventNum_t startEventNum,
		       size_t numEvents, size_t eventBufSize,
		       float rateGbps)
{    
    // Convert bit rate to event rate:
    
    float eventRate{rateGbps*1000000000/(eventBufSize*8)};
    u_int64_t interEventSleepUsec{
	static_cast<u_int64_t>(eventBufSize*8/(rateGbps * 1000))
    };

    // To help print large integers:
    
    std::cout.imbue(std::locale(""));

    std::cout << "Sending bit rate is " << rateGbps << " Gbps" << std::endl;
    std::cout << "Event size is " << eventBufSize << " bytes or "
	      << eventBufSize*8 << " bits" << std::endl;
    std::cout << "Event rate is " << eventRate << " Hz" << std::endl;
    std::cout << "Inter-event sleep time is " << interEventSleepUsec
	      << " microseconds" << std::endl;
    std::cout << "Sending " << numEvents << " event buffers" << std::endl;
    std::cout << "Using MTU " << s.getMTU() << std::endl;

    if (s.getMaxPldLen() < eventPldStart.size() + eventPldEnd.size())
        return E2SARErrorInfo{
	    E2SARErrorc::LogicError, "MTU is too short to send needed payload"
	};
    
    // Start threads, open sockets. Start sending sync packets:
    
    auto openRes = s.openAndStart();
    if (openRes.has_error()) {
        return openRes;
    } else {
	// Note we have to get the _value_ of the return object:
	std::cout << "Started segmenter with result "
		  << openRes.value() << std::endl;
    }

    // Initialize a pool of memory buffers  we will be sending (mostly filled
    // with random data):
    
    evtBufferPool = new boost::pool<>{eventBufSize};

    // Sleep to allow small number of frames to leave:
    
    boost::chrono::seconds duration(1);
    boost::this_thread::sleep_for(duration);

    ////
    // Send loop
    //
    for(size_t evt = 0; evt < numEvents; evt++)
    {
        // Get the current time point:
	
        auto now = boost::chrono::high_resolution_clock::now();

        // Send the event:
	
        auto eventBuffer = static_cast<u_int8_t*>(evtBufferPool->malloc());
	
        // Fill in the first part of the buffer with something meaningful
	// and also the end. The rest of it is whatever random data:
	
        memcpy(eventBuffer, eventPldStart.c_str(), eventPldStart.size());
        memcpy(eventBuffer + eventBufSize - eventPldEnd.size(),
	       eventPldEnd.c_str(), eventPldEnd.size());

        // Put on queue with a callback to free this buffer. We probably want
	// to fill in the data ID with something more meaningful here:
	
        auto sendRes = s.addToSendQueue(eventBuffer, eventBufSize, evt, 0, 0,
					&freeBuffer, eventBuffer);

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
            evtBufferPool->free(item);
	}
        boost::this_thread::sleep_until(until);
    }

    // Done sending events, report:
   
    auto stats = s.getSendStats();

    evtBufferPool->purge_memory();
    std::cout << "Completed, " << stats.get<0>() << " frames sent, "
	      << stats.get<1>() << " errors" << std::endl;
    if (stats.get<1>() != 0)
    {
        std::cout << "Last error encountered: "
		  << strerror(stats.get<2>()) << std::endl;
    }
    
    return 0;
}

/**
 * @brief Listen for events.
 */
result<int> recvEvents(Reassembler &r, int durationSec) {

    std::cout << "Receiving on ports " << r.get_recvPorts().first
	      << ":" << r.get_recvPorts().second << std::endl;

    u_int8_t* evtBuf{nullptr};
    size_t evtSize;
    EventNum_t evtNum;
    u_int16_t dataId;

    // Register the worker (will be NOOP if withCP is set to false):
    
    auto hostname_res = NetUtil::getHostName();
    if (hostname_res.has_error()) 
    {
        return E2SARErrorInfo{hostname_res.error().code(),
	    hostname_res.error().message()};
    }
    auto regres = r.registerWorker(hostname_res.value());
    if (regres.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError, 
            "Unable to register worker node due to "
	    + regres.error().message()};
    }
    if (regres.value() == 1)
        std::cout << "Registered the worker" << std::endl;

    // NOTE: if we switch the order of registerWorker and openAndStart
    // you get into a race condition where the sendState thread starts and tries
    // to send queue updates, however session token is not yet available...
    auto openRes = r.openAndStart();
    if (openRes.has_error()) {
        return openRes;
    } else {
	std::cout << "Started reassembler with result "
		  << openRes.value() << std::endl;
    }

    // To help print large integers:
    
    std::cout.imbue(std::locale(""));

    auto now = boost::chrono::steady_clock::now();

    ////
    // Receive loop
    //
    while(true)
    {
	// Wait 1 s (1000 ms) for next event:
	
        auto getEvtRes = r.recvEvent(&evtBuf, &evtSize, &evtNum, &dataId, 1000);

        auto next = boost::chrono::steady_clock::now();

	// If duration is set stop listening after that time and exit:
	
        if ((durationSec != 0)
	    && (next - now > boost::chrono::seconds(durationSec)))
        {
            ctrlCHandler(0);
            break;
        }

	// Read error:
	
        if (getEvtRes.has_error())
            return getEvtRes;

        if (getEvtRes.value() == -1) { // Queue is empty
            continue;
	}

	// Data validation: check that the beginning and end of the payload
	// look as we expect:
      
        if (memcmp(evtBuf, eventPldStart.c_str(), eventPldStart.size() - 1))
            return E2SARErrorInfo{E2SARErrorc::MemoryError,
		"Payload start does not match expected"};

        if (memcmp(evtBuf + evtSize - eventPldEnd.size(), eventPldEnd.c_str(),
		   eventPldEnd.size() - 1))
            return E2SARErrorInfo{E2SARErrorc::MemoryError,
		"Payload end doesn't match expected"};

        delete evtBuf; // Isn't this dangling ??
	evtBuf = nullptr; // Fixed?
    }
    
    std::cout << "Completed" << std::endl;
    
    return 0;
}

void recvStatsThread(Reassembler *r)
{
    std::vector<std::pair<EventNum_t, u_int16_t>> lostEvents;

    while(threadsRunning)
    {
        auto now = boost::chrono::high_resolution_clock::now();

        auto stats = r->getStats();

        while(true)
        {
            auto res = r->get_LostEvent();
            if (res.has_error())
                break;
            lostEvents.push_back(res.value());
        }

        std::cout << "Stats:" << std::endl;
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
 * @brief Point-to-point Segmenter/Reassembler framework for testing on 
 * localhost. Largely copied/derived from e2sar_perf.cpp.
 */
int main(int argc, char* argv[])
{
    po::options_description od("Command-line options");
    
    auto opts = od.add_options()("help,h", "show this help message");

    // Segmenter and Reassembler flags and configuration options.
    // In principle many if not all of these should be specified in
    // a configuration file or on the command line with reasonable
    // default values, but for now:
	
    bool withCP(false);                // false for no LB point-to-point
    u_int16_t mtu(1500);               // Max transmission unit (bytes)
    int sockBufSize(1024*1024*3);      // 3 Mb
    size_t numSockets(4);              // Number of send sockets
    bool zeroRate(false);              // Report zero rate in Sync messages
    bool usecAsEventNum(false);        // Use usec clock as event number
    bool validate(true);               // Validate server certificate
    bool preferHostAddr(false);        // Prefer IPv4 or IPv6
    size_t numThreads(1);              // Receiver threads
    bool preferV6(false);              // Prefer ipv6 over ipv4
    float rateGbps(1.0);               // Send rate in Gpps    
    size_t eventBufferSize(1024*1024); // 1 Mb
    EventNum_t startingEventNum{0};    // First event #
    u_int32_t eventSourceId(1234);     // Event "color"                 --?
    u_int16_t dataId(4321);            // Like a channel ID, I think    --?
    int durationSec(0);                // Receive data duration, 0 is forever

    std::string sndrcvIp;    // Defaults to `localhost`
    u_int16_t recvStartPort; // Defaults to 10000
    size_t numEvents;        // Number of events to send
        
    opts("send,s", "send traffic");
    opts("recv,r", "receive traffic");

    // Only a few options for now, mostly to direct traffic:
    
    opts("uri,u",
	 po::value<std::string>(),
	 "URI from the command line to override EJFAT_URI envvar");
    opts("ip",
	 po::value<std::string>(&sndrcvIp)->default_value("127.0.0.1"),
	 "IP address (IPv4 or IPv6) from which sender sends from or on which "
	 "receiver listens. Defaults to 127.0.0.1. [s,r]");
    opts("port",
	 po::value<u_int16_t>(&recvStartPort)->default_value(10000),
	 "Starting UDP port number on which receiver listens. Defaults to "
	 "10000. [r] ");
    opts("num,n",
	 po::value<size_t>(&numEvents)->default_value(10),
	 "Number of event buffers to send. Defaults to 10 [s]");
    
    po::variables_map vm; // Command line options stored here.
    po::store(po::parse_command_line(argc, argv, od), vm);
    po::notify(vm);

    // Ctrl-C signal:

    signal(SIGINT, ctrlCHandler);

    // Read in the options:
    
    std::cout << "E2SAR Version: " << get_Version() << std::endl;
    if (vm.count("help"))
    {
        std::cout << od << std::endl;
        return EXIT_SUCCESS;
    }

    try {
        conflicting_options(vm, "send", "recv");
	conflicting_options(vm, "send", "port");
    }
    catch (const std::logic_error& e) {
	std::cerr << "Error processing command-line options: "
		  << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // Make sure the token is interpreted as the correct type,
    // depending on the call:
    
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

    // Ready to go, set up transmission elements:
    
    if (vm.count("send") || vm.count("recv"))  {

	// The URI needs to be enclosed in quotes to avoid interpreting '&' as
	// a shell command. Here we also demonstrate the standard way to
	// check return codes for API calls:

	auto uri_r = (
	    vm.count("uri") ?
	    EjfatURI::getFromString(vm["uri"].as<std::string>(), tt, preferV6)
	    : EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6)
	    );
	
	if (uri_r.has_error())
	{
	    std::cerr << "Error in parsing URI from command-line, error "s
		+ uri_r.error().message() << std::endl;
	    return EXIT_FAILURE;
	}
	
	auto uri = uri_r.value(); // Get the object from the return type.

	if (vm.count("send")) {

	    // Send behavior:
	    
	    Segmenter::SegmenterFlags sflags;
	    sflags.useCP = withCP; 
	    sflags.mtu = mtu;
	    sflags.sndSocketBufSize = sockBufSize;
	    sflags.numSendSockets = numSockets;
	    sflags.zeroRate = zeroRate;
	    sflags.usecAsEventNum = usecAsEventNum;
	
	    std::cout << "Control plane                "
		      << (sflags.useCP ? "ON" : "OFF") << std::endl;
	    std::cout << "Event rate reporting in Sync "
		      << (sflags.zeroRate ? "OFF" : "ON") << std::endl;
	    std::cout << "Using usecs as event numbers "
		      << (sflags.usecAsEventNum ? "ON" : "OFF") << std::endl;
	    std::cout << "Number of send sockets:      "
		      << sflags.numSendSockets << std::endl;
	    std::cout << (sflags.useCP ?
			  "*** Make sure the LB has been reserved and the URI "
			  "reflects the reserved instance information."
			  : "*** Make sure the URI reflects proper data "
			  "address, other parts are ignored.") << std::endl;

	    try {
	        Segmenter seg(uri, dataId, eventSourceId, sflags);
	        segPtr = &seg;
	        auto res = sendEvents(
		    seg, startingEventNum, numEvents, eventBufferSize, rateGbps
		    );
	        if (res.has_error()) {
	            std::cerr << "Segmenter encountered an error: "
			      << res.error().message() << std::endl;
	        }
	    } catch (const E2SARException& e) {
	        std::cerr << "Unable to create segmenter: "
			  << static_cast<std::string>(e) << std::endl;
		exit(EXIT_FAILURE);
	    }
	} else if (vm.count("recv")) {
	    
	    // Receive behavior:
	    
	    Reassembler::ReassemblerFlags rflags;
	    rflags.useCP = withCP;
	    rflags.withLBHeader = not withCP;
	    rflags.rcvSocketBufSize = sockBufSize;
	    rflags.useHostAddress = preferHostAddr;
	    rflags.validateCert = validate;

	    std::cout << "Control plane will be "
		      << (rflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << (rflags.useCP ?
			  "*** Make sure the LB has been reserved and the URI "
			  "reflects the reserved instance information."
			  : "*** Make sure the URI reflects proper data "
			  "address, other parts are ignored.") << std::endl;

	    try {
                ip::address ip = ip::make_address(sndrcvIp);
                Reassembler reas(uri, ip, recvStartPort, numThreads, rflags);
                reasPtr = &reas;
                boost::thread statsThread(&recvStatsThread, &reas);
                auto res = recvEvents(reas, durationSec);
                if (res.has_error()) {
                    std::cerr << "Reassembler encountered an error: "
			      << res.error().message() << std::endl;
                }
            } catch (E2SARException &e) {
                std::cerr << "Unable to create reassembler: "
			  << static_cast<std::string>(e) << std::endl;
		exit(EXIT_FAILURE);
	    }

	}
    } // 'send' or 'receive'
    
    return 0;
}
