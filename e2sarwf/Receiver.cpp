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
 * @file Receiver.cpp
 * @brief Implement the data receiver class
 */

#include "Receiver.h"

#include <csignal>
#include <filesystem>
#include <iostream>

#include <e2sar.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <DataFormat.h> // From UFMT

#include <Exception.h>  // From NSCLDAQ

#include "FribE2sarUtils.h"
#include "BufferedSink.h"

using namespace e2sar;
using namespace frib_e2sar;
using namespace ufmt;

namespace po = boost::program_options;
namespace ch = boost::chrono;
namespace pt = boost::posix_time;

Receiver* Receiver::m_pInstance = nullptr;

Receiver::Receiver(po::variables_map& vm) :
    m_proto(vm["proto"].as<std::string>()),
    m_hostName(vm["hostname"].as<std::string>()),
    m_basePath(vm["basepath"].as<std::string>()),
    m_sinkName(vm["sinkname"].as<std::string>()),
    m_duration(vm["duration"].as<int>()),
    m_numThreads(vm["deq"].as<size_t>()),
    m_threadsRunning(false),
    m_debug(vm["debug"].as<bool>()),
    m_verbose(vm["verbose"].as<bool>())
{
    /////////////////////////////////////////////////////////////////////////
    // Set instance and register signal handler
    //

    if (m_pInstance) {
	throw std::runtime_error("Receiver instance already exists!");
    } else {
	setInstance(this);
	std::signal(SIGINT, ctrlCHandler);
    }

    /////////////////////////////////////////////////////////////////////////
    // Configure data sink
    //

    m_pSink = std::make_unique<BufferedSink>(makeSinkUri());

    /////////////////////////////////////////////////////////////////////////
    // Read ini file and get flags
    //

    std::string iniFile;
    if (vm.count("ini")) {
	iniFile = vm["ini"].as<std::string>();
    } else {
	auto cwd = std::filesystem::current_path();
	iniFile = cwd.string() + "/reassembler_config.ini";
    }
    
    auto flags = getReassemblerFlagsFromFile(iniFile);

    /////////////////////////////////////////////////////////////////////////
    // Create EJFAT URI
    //
    
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};
    bool preferV6 = false; // For now always Ipv4
    auto ejfatUri = getUri(vm["uri"].as<std::string>(), tt, preferV6);
    
    /////////////////////////////////////////////////////////////////////
    // Instantiate Reassembler
    //

    auto ip_s = vm["ip"].as<std::string>();
    auto port = vm["port"].as<u_int16_t>();
    auto numThreads = vm["threads"].as<size_t>(); // Reassembler
    auto deqThreads = vm["deq"].as<size_t>();     // Dequeue/read

    ip::address ip = ip::make_address(ip_s);    
    m_pReassembler
	= std::make_unique<Reassembler>(ejfatUri, ip, port, numThreads, flags);
    
    if (m_verbose) {
	std::cout << "----- Receiver configuration -----" << std::endl;
	printReassemblerFlags(flags);
     }
}

Receiver::~Receiver()
{
    shutdown();
}

int
Receiver::operator()()
{
    m_threadsRunning = true;
    
    boost::thread statsThread(boost::bind(&Receiver::statsThread, this));
    
    if (prepareToReceive()) {
	throw std::runtime_error("Failed to initialize and start Reassembler");
    }    

    for (size_t i = 0; i < m_numThreads; i++) {
	m_deqThreads.push_back(boost::thread(&Receiver::receiveEvents, this));
    }
    
    for (auto& t : m_deqThreads) {
     	t.join();
    }
    
    return EXIT_SUCCESS;
}

/**
 * @details 
 * Re-raises default signal after shutdown
 */
void
Receiver::ctrlCHandler(int sig) 
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
Receiver::shutdown()
{   
    std::cout << "Stopping receiver threads..." << std::endl;
    m_threadsRunning = false;
    ch::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);
    
    if (m_pReassembler) {
	std::cout << "Deregistering worker..." << std::endl;
	auto rv = m_pReassembler->deregisterWorker();
	if (rv.has_error()) {
	    std::cout << "Unable to deregister worker on exit: "
		      << rv.error().message() << std::endl;
	} else {
	    std::cout << "Worker deregistered" << std::endl;
	}
	m_pReassembler->stopThreads();
    }

    std::cout << "Stopping dequeue threads..." << std::endl;
    std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& t : m_deqThreads) {
	t.interrupt();
    }
    for (auto& t : m_deqThreads) {
	t.join();	
    }

    std::cout << "Stopping output thread..." << std::endl;
    m_pSink->stopThreads();
    
    boost::this_thread::sleep_for(duration);
}

/**
 * @details
 * Typically to be called as part of a monitoring thread. There is no 
 * signaling mechanism between threads: requires m_threadsRunning == true 
 * when the caller thread starts or there is no output. The stats types are:
 *  - 0 EventNum_t enqueueLoss;
 *  - 1 EventNum_t reassemblyLoss;
 *  - 2 EventNum_t eventSuccess;
 *  - 3 int lastErrno; 
 *  - 4 int grpcErrCnt; 
 *  - 5 int dataErrCnt; 
 *  - 6 E2SARErrorc lastE2SARError; 
 */
void
Receiver::statsThread()
{    
    std::vector<boost::tuple<EventNum_t, u_int16_t, size_t>> lostEvents;

    while (m_threadsRunning) {
	auto now = ch::high_resolution_clock::now();
	
        auto stats = m_pReassembler->getStats();
	
        while(true) {
            auto rvle = m_pReassembler->get_LostEvent();
            if (rvle.has_error()) {
                break;
	    }
            lostEvents.push_back(rvle.value());
        }

	pt::ptime currentTime(pt::second_clock::local_time());
	
	std::cout << pt::to_simple_string(currentTime)
		  << " Stats:" << std::endl;
        std::cout << "\tEvents Received: " << stats.eventSuccess << std::endl;
        std::cout << "\tEvents Lost in reassembly: "
		  << stats.reassemblyLoss << std::endl;
        std::cout << "\tEvents Lost in enqueue: "
		  << stats.enqueueLoss << std::endl;
        std::cout << "\tData Errors: " << stats.dataErrCnt << std::endl;
	
        if (stats.dataErrCnt > 0) {
            std::cout << "\tLast Data Error: "
		      << strerror(stats.lastErrno) << std::endl;
	}
        std::cout << "\tgRPC Errors: " << stats.grpcErrCnt << std::endl;
	
        if (stats.lastE2SARError != E2SARErrorc::NoError) {
            std::cout << "\tLast E2SARError code: "
		      << make_error_code(stats.lastE2SARError).message()
		      << std::endl;
	}

	if (m_debug) {
	    std::cout << "\tEvents lost so far "
		"(<Evt ID:Data ID/num frags rcvd>): ";
	    for(auto evt: lostEvents) {
		std::cout << "<" << evt.get<0>() << ":"
			  << evt.get<1>() << "/" << evt.get<2>() << "> ";
	    }
	    std::cout << std::endl;
	} else {
	    std::cout << "\tEvents lost so far: "
		      << lostEvents.size() << std::endl;
	}

        auto until = now + ch::milliseconds(2000);
        boost::this_thread::sleep_until(until);
    }  
}

/** 
 * @details 
 * Per E2SAR collaboration: If we switch the order of `registerWorker()` and 
 * `openAndStart()` you get into a race condition where the sendState thread 
 * starts and tries to send queue updates, however the session token is not 
 * yet available...
 */
int
Receiver::prepareToReceive()
{
    if (m_verbose) {
	std::cout << "Receiving on ports "
		  << m_pReassembler->get_recvPorts().first << ":" 
		  << m_pReassembler->get_recvPorts().second << std::endl;
    }

    // Worker registration is NOP if not using control plane:
    
    auto rvhn = NetUtil::getHostName();
    if (rvhn.has_error()) {
	std::cerr << "Failed to get hostname: " << rvhn.error().message()
		  << " with error code " << rvhn.error().code() << std::endl;
	return EXIT_FAILURE;
    }

    auto rvrw = m_pReassembler->registerWorker(rvhn.value());
    if (rvrw.has_error()) {
	std::cerr << "Unable to register worker: " << rvrw.error().message()
		  << " with error code " << rvrw.error().code() << std::endl;
        return EXIT_FAILURE;
    }

    boost::this_thread::sleep_for(ch::seconds(1));
    
    auto rvoas = m_pReassembler->openAndStart();
    if (rvoas.has_error()) {
     	std::cerr << "Unable to start Reassembler: " << rvoas.error().message()
		  << " with error code " << rvoas.error().code() << std::endl;
	return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

int
Receiver::receiveEvents()
{
    // Received event information and receiver config. The extent of good
    // data for a particular event is defined by evtBufSize.

    u_int8_t*  evtBuf{nullptr}; // Event buffer for data reads
    size_t     evtBufSize;      // Event buffer size in bytes
    EventNum_t evtNum;          // Event number
    u_int16_t  dataId;          // Data source Id
    size_t     totalBytes = 0;  // Total bytes written
    
    auto start = ch::steady_clock::now();

    try {
	while (true) {
	    boost::this_thread::interruption_point();

	    // evtBuf points either to memory allocated in the Reassembler
	    // (or nullptr if no data to output is queued):

	    auto rv = m_pReassembler->getEvent(&evtBuf, &evtBufSize,
					       &evtNum, &dataId);

	    auto now = ch::steady_clock::now();
	    if (m_duration != 0	&& (now - start) > ch::seconds(m_duration)) {
		throw boost::thread_interrupted();
	    }
	
	    if (rv.has_error()) {
		std::cerr << "Reassembler failed to get event: "
			  << rv.error().message() << " with error code "
			  << rv.error().code() << std::endl;
		return EXIT_FAILURE;
	    }

	    if (rv.value() == -1) { // Queue is empty
		continue;
	    }

	    totalBytes += evtBufSize;
		
	    if (m_debug) {
		// dumpBuffer(evtBuf, evtBufSize);
		std::cout << "Received event:  " << std::endl;
		std::cout << "\tevtNumber:    " << evtNum << std::endl;
		std::cout << "\tdataId:       " << dataId << std::endl;
		std::cout << "\tevtBufSize:   " << evtBufSize << std::endl;
		std::cout << "\ttotalBytes:   " << totalBytes << std::endl;
	    }

	    // Hands data off to sink, which is now responsible for deletion:

	    m_pSink->addData(evtNum, evtBuf, evtBufSize);
	}
    }
    catch (const boost::thread_interrupted& e) {
	std::cout << "Interrupted dequeue thread "
		  << boost::this_thread::get_id()
		  << std::endl;
    }
    
    return EXIT_SUCCESS;
}

std::string
Receiver::makeSinkUri()
{
    char uri[1024]; // Hopefully big enough...
    if (m_proto == "ring" || m_proto == "tcp") {
	sprintf(uri, "%s://%s/%s", m_proto.c_str(),
		m_hostName.c_str(), m_sinkName.c_str());
    } else if (m_proto == "file") {
	sprintf(uri, "%s://%s/%s.evt", m_proto.c_str(),
		m_basePath.c_str(), m_sinkName.c_str());
    }
    
    return std::string(uri);
}
