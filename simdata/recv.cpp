/** 
 * @file recv.cpp
 * @brief Simple receive with no load balancer.
 */

#include <iostream>
#include <cstddef>
#include <string>

#include <boost/program_options.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <e2sar.hpp>
#include <e2sarDPReassembler.hpp>

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

// Event payload:

uint16_t evtPldStart = 0x1234;
uint16_t evtPldEnd = 0xabcd;

// Other global config:

bool threadsRunning(true);
u_int16_t reportThreadSleepMs{2000}; // 2 second maximum
Reassembler* reasPtr{nullptr};

void ctrlCHandler(int sig) 
{
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

po::variables_map
getOpts(int ac, char* av[])
{
    // Configure and parse command-line options:
     
    po::options_description od("Send command-line options");
    auto opts = od.add_options()
	("help,h", "show command help")
	("config-file,c",
	 po::value<std::string>()->default_value("./reassembler_config.ini"),
	 "path to configuration file")
	("uri,u",
	 po::value<std::string>(),
	 "URI from the command line to override EJFAT_URI envvar")
	("ip",
	 po::value<std::string>()->default_value("127.0.0.1"),
	 "IP address (IPv4 or IPv6) on which receiver listens.")
	("port",
	 po::value<u_int16_t>()->default_value(10000),
	 "starting UDP port number on which receiver listens.")
	("preferV6",
	 po::value<bool>()->default_value(false),
	 "prefer IPv6 over IPv4")
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

Reassembler::ReassemblerFlags
getFlags(const po::variables_map& opts)
{
    auto flags_rv = Reassembler::ReassemblerFlags::getFromINI(
	opts["config-file"].as<std::string>()
	);
    if (flags_rv.has_error()) {
	std::cerr << "Error reading configuration file: "s
	    + flags_rv.error().message() << std::endl;
	exit(EXIT_FAILURE);
    }

    auto flags = flags_rv.value();
    
    // Print out some info about the flags:

    std::cout << "Control plane will be "
	      << (flags.useCP ? "ON" : "OFF") << std::endl;
    std::cout << (flags.useCP ?
		  "*** Make sure the LB has been reserved and the URI "
		  "reflects the reserved instance information."
		  : "*** Make sure the URI reflects proper data "
		  "address, other parts are ignored.") << std::endl;
    
    return flags;
}

void
printFlags(const Reassembler::ReassemblerFlags& flags)
{
    std::cout << "Reassembler flags:\n";
    std::cout <<"\tuseCP\t\t" << flags.useCP << std::endl;
    std::cout <<"\tuseHostAddress\t" << flags.useHostAddress << std::endl;
    std::cout <<"\tperiod_ms\t" << flags.period_ms << std::endl;
    std::cout <<"\tvalidateCert\t" << flags.validateCert << std::endl;
    std::cout <<"\tKi, Kp, Kd\t" << flags.Ki << ", " << flags.Kp
	      << ", " << flags.Kd << std::endl;
    std::cout <<"\tsetPoint\t" << flags.setPoint << std::endl;
    std::cout <<"\tepoch_ms\t" << flags.epoch_ms << std::endl;
    std::cout <<"\tportRange\t" << flags.portRange << std::endl;
    std::cout <<"\twithLBHeader\t" << flags.withLBHeader << std::endl;
    std::cout <<"\teventTimeout_ms\t" << flags.eventTimeout_ms << std::endl;
    std::cout <<"\trcvSocketBufSize\t" << flags.rcvSocketBufSize
	      << " (bytes)" << std::endl;
    std::cout <<"\tweight\t\t" << flags.weight << std::endl;
    std::cout <<"\tmin_factor\t" << flags.min_factor << std::endl;
    std::cout <<"\tmax_factor\t" << flags.max_factor << std::endl;
}

result<int> recvEvents(Reassembler &r, int durationSec) {

    std::cout << "Receiving on ports " << r.get_recvPorts().first
	      << ":" << r.get_recvPorts().second << std::endl;

    u_int8_t* evtBuf{nullptr};
    size_t evtBufSize;
    EventNum_t evtNum;
    u_int16_t dataId;

    // We assume for now that the CP is disabled:
    
    auto open_rv = r.openAndStart();
    if (open_rv.has_error()) {
        return open_rv;
    }
    
    std::cout.imbue(std::locale(""));
    auto now = boost::chrono::steady_clock::now();

    /////////////////////////////////////////////////////////////////////////
    // Receive loop
    ///
    
    while(true)
    {
	// Wait 2 s (2000 ms) for next event:
	
        auto recv_rv = r.recvEvent(&evtBuf, &evtBufSize, &evtNum,
				   &dataId, 2000);
        auto next = boost::chrono::steady_clock::now();

	// If duration is set stop listening after that time and exit:
	
        if (
	    (durationSec != 0)
	    && ((next - now) > boost::chrono::seconds(durationSec))
	    )
        {
            ctrlCHandler(0);
            break;
        }

	// Read error:
	
        if (recv_rv.has_error())
            return recv_rv;

        if (recv_rv.value() == -1) { // Queue is empty
            continue;
	}

	// Data validation: check that the beginning and end of the payload
	// look as we expect:
      
        if (memcmp(evtBuf, &evtPldStart, sizeof(evtPldStart))) {
	    for (int i=0; i<sizeof(evtPldStart); i++) {
		std::cout << (int)evtBuf[i] << " " << std::endl;
	    }
            return E2SARErrorInfo{E2SARErrorc::MemoryError,
		"Payload start does not match expected"};
	}
        if (memcmp(evtBuf + evtBufSize - sizeof(evtPldEnd),
		   &evtPldEnd, sizeof(evtPldEnd))) {
            return E2SARErrorInfo{E2SARErrorc::MemoryError,
		"Payload end doesn't match expected"};
	}
	
        delete evtBuf;
	evtBuf = nullptr;
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


int main(int argc, char* argv[])
{    
    auto opts = getOpts(argc, argv); // Command-line options.
    signal(SIGINT, ctrlCHandler); // Ctrl-C signal:

    /////////////////////////////////////////////////////////////////////////
    // Setup
    ///
    
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

    // Configure reassembler options:
    
    auto preferV6 = opts["preferV6"].as<bool>();
    auto ip_s = opts["ip"].as<std::string>();
    auto port = opts["port"].as<u_int16_t>();
    int durationSec = 0;  // Receive duration, 0 is forever
    size_t numThreads(1); // Receiver threads
    auto flags = getFlags(opts);
    auto uri = getURI(opts, tt, preferV6);

    if (opts.count("debug")) {
	std::cout << "Using E2SAR version: " << get_Version() << std::endl;
	printFlags(flags);
	std::cout << "Using URI: " << uri.to_string() << std::endl;
    }

    // Instantiate and run:
    
    try {
	ip::address ip = ip::make_address(ip_s);
	Reassembler reas(uri, ip, port, numThreads, flags);
	reasPtr = &reas;
	boost::thread statsThread(&recvStatsThread, &reas);
	auto recv_rv = recvEvents(reas, durationSec);
	 if (recv_rv.has_error()) {
	     std::cerr << "Reassembler encountered an error: "
		       << recv_rv.error().message() << std::endl;
	 }
    } catch (E2SARException &e) {
	std::cerr << "Unable to create reassembler: "
		  << static_cast<std::string>(e) << std::endl;
	exit(EXIT_FAILURE);
    }
    
    return EXIT_SUCCESS;
}
