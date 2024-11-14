/** 
 * @file recv.cpp
 * @brief Simple receive with no load balancer.
 */

#include <iostream>
#include <cstddef>
#include <string>

#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <e2sar.hpp>
#include <e2sarDPReassembler.hpp>

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

//#include <CDataSink.h>
//#include <CFileDataSink.h>
#include <Exception.h>

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;
using namespace ufmt;

const size_t MAX_BODY = 8192; // Largest ring item body in bytes

// Other global config:

bool threadsRunning(true);
u_int16_t reportThreadSleepMs{2000}; // 2 second maximum
Reassembler* reasPtr{nullptr};

void
dumpBuffer(u_int8_t* buf, size_t bytes) {
    std::cout << "------------------------------------------" << std::endl;
    for (auto i = 0; i < bytes; i++) {
	if (i != 0 && i%8 == 0) {
	    printf("\n");
	}
	printf("%02x ", (unsigned)buf[i]);
    }
    std::cout << std::dec << std::endl;
}

void
ctrlCHandler(int sig) 
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
	// ("sink,S",
	//  po::value<std::string>()->required(),
	//  "path to output file data sink (*not* a URI)")
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
 * @brief Map the version we get from the command line to a factory version.
 * @param fmtIn Format the user requested.
 * @throw std::invalid_argument Bad format version
 * @return Factory version ID (from the enum).
 * @note We should never throw because gengetopt will enforce the enum.
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
	return FormatSelector::v10;
    default:
	throw std::invalid_argument("Invalid DAQ format version specifier");
    }
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

result<int>
recvEvents(Reassembler &r, /*CFileDataSink* pSink,*/ RingItemFactoryBase& factory,
	   int durationSec, bool debug=false) {

    std::cout << "Receiving on ports " << r.get_recvPorts().first
	      << ":" << r.get_recvPorts().second << std::endl;

    // Received event information and receiver config:
    
    u_int8_t*  evtBuf{nullptr}; // Event buffer
    size_t     evtBufSize;      // Event buffer size in bytes
    EventNum_t evtNum;          // Event number (typically timestamp)
    u_int16_t  dataId;          // Data Id (source Id or other)
    u_int64_t  waitMs = 2000;   // Wait time in milliseconds

    // We assume for now that the CP is disabled:
    
    auto open_rv = r.openAndStart();
    if (open_rv.has_error()) {
        return open_rv;
    }
    
    auto now = boost::chrono::steady_clock::now();

    /////////////////////////////////////////////////////////////////////////
    // Receive loop
    ///
    
    while(true)
    {
	// Blocking receive. Use getEvent() for non-blocking:

        auto recv_rv = r.recvEvent(&evtBuf, &evtBufSize, &evtNum,
				   &dataId, waitMs);
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

	// Data post-processing. The event buffer is a complete ring item.

	u_int8_t* p = evtBuf;
	auto pHdr = reinterpret_cast<RingItemHeader*>(p);
	p += sizeof(RingItemHeader);
	auto pBodyHdr = reinterpret_cast<BodyHeader*>(p);
	p += pBodyHdr->s_size; // 20 or sizeof(uint32_t)

	if(pHdr->s_type == PHYSICS_EVENT) {
	    std::unique_ptr<CRingItem> pItem(
		factory.makeRingItem(pHdr->s_type, pBodyHdr->s_timestamp,
				     pBodyHdr->s_sourceId, MAX_BODY,
				     pBodyHdr->s_barrier)
		);
	    u_int8_t* pBody = reinterpret_cast<u_int8_t*>(
		pItem->getBodyCursor()
		);
	    int bodySize = evtBufSize - sizeof(RingItemHeader)
		- pBodyHdr->s_size;
	    memcpy(pBody, p, bodySize);
	    pBody += bodySize;	    
	    pItem->setBodyCursor(pBody);
	    pItem->updateSize();
	    
	    std::cout << pItem->toString() << std::endl;
	}
		
	if (debug) {
	    std::cout << "Receive event:" << std::endl;
	    std::cout << "\tevtNumber:  " << evtNum << std::endl;
	    std::cout << "\tdataId:     " << dataId << std::endl;
	    std::cout << "\tevtBufSize: " << evtBufSize << std::endl;

	    std::cout << "Header:" << std::endl;
	    std::cout << "\tsize: " << pHdr->s_size
		      << "\n\ttype: " << pHdr->s_type
		      << std::endl;
	    std::cout << "Body header with size: " << pBodyHdr->s_size
		      << std::endl;
	    if (pBodyHdr->s_size > sizeof(uint32_t)) {
		std::cout << "\ttimestamp: " << pBodyHdr->s_timestamp
			  << "\n\tsourceId: " << pBodyHdr->s_sourceId
			  << "\n\tbarrier: " << pBodyHdr->s_barrier
			  << std::endl;
	    } else {
		std::cout << "\tEmpty body header" << std::endl;
	    }
	}
		
	// Cleanup:
	
        delete evtBuf;
	evtBuf = nullptr;
    }	
        
    return 0;
}

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


int
main(int argc, char* argv[])
{    
    auto opts = getOpts(argc, argv); // Command-line options.
    signal(SIGINT, ctrlCHandler); // Ctrl-C signal:

    try {

	/////////////////////////////////////////////////////////////////////
	// Configure data sink
	///

	FormatSelector::SupportedVersions version
	    = mapVersion(opts["nscldaq-version"].as<int>());
	auto& factory = FormatSelector::selectFactory(version);
	
	//auto name = opts["sink"].as<std::string>();
	//std::unique_ptr<CFileDataSink> pSink(new CFileDataSink(name));
	
	/////////////////////////////////////////////////////////////////////
	// Configure E2SAR
	///

	EjfatURI::TokenType tt{EjfatURI::TokenType::instance};
    
	auto preferV6 = opts["preferV6"].as<bool>();
	auto ip_s = opts["ip"].as<std::string>();
	auto port = opts["port"].as<u_int16_t>();
	int durationSec = 0;  // Receive duration, 0 is forever
	size_t numThreads(1); // Receiver threads
	auto flags = getFlags(opts);
	auto uri = getURI(opts, tt, preferV6);
	bool debug = opts.count("debug");    

	if (debug) {
	    std::cout << "Using E2SAR version: " << get_Version() << std::endl;
	    printFlags(flags);
	    std::cout << "Using URI: " << uri.to_string() << std::endl;
	}
    
	/////////////////////////////////////////////////////////////////////
	// Instantiate and run Segmenter:
	///
	
	ip::address ip = ip::make_address(ip_s);
	Reassembler reas(uri, ip, port, numThreads, flags);
	reasPtr = &reas;
	boost::thread statsThread(&recvStatsThread, &reas);
	auto recv_rv = recvEvents(reas, /*pSink.get(),*/ factory, durationSec, debug);
	if (recv_rv.has_error()) {
	    std::cerr << "Reassembler encountered an error: "
		      << recv_rv.error().message() << std::endl;
	}
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
