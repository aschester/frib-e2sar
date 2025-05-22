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

// E2SAR includes and deps:

#include <e2sar.hpp>

// Unified format library:

#include <DataFormat.h>

// Other NSCLDAQ includes:

#include <URL.h>
#include <Exception.h>

// Project headers:

#include "FribE2sarUtils.h"
#include "DataSink.h"
#include "FileDataSink.h"
#include "RingDataSink.h"

using namespace e2sar;
using namespace frib_e2sar;
using namespace ufmt;

Receiver* Receiver::m_pInstance = nullptr;

Receiver::Receiver(po::variables_map& vm) :
    m_proto(vm["proto"].as<std::string>()),
    m_hostName(vm["hostname"].as<std::string>()),
    m_basePath(vm["basepath"].as<std::string>()),
    m_baseName(vm["basename"].as<std::string>()),
    m_duration(vm["duration"].as<int>()),
    m_deqThreads(vm["deq"].as<size_t>()),
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

    auto daqVersion = vm["nscldaq-version"].as<int>();    
    FormatSelector::SupportedVersions version = mapVersion(daqVersion);
    auto& factory = FormatSelector::selectFactory(version);

    /////////////////////////////////////////////////////////////////////////
    // Read ini file and get flags
    //

    std::string iniFile;
    if (!vm.count("ini")) {
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

    std::vector<boost::thread> threads;
    std::vector<std::unique_ptr<DataSink>> sinks;
    for (size_t i = 0; i < m_deqThreads; i++) {
	std::unique_ptr<DataSink> pSink(makeDataSink(i));
	boost::thread t(
	    std::bind(&Receiver::receiveEvents, this, pSink.get())
	    );
	threads.push_back(std::move(t));
	sinks.push_back(std::move(pSink));
    }
    
    for (auto& t : threads) {
     	t.join();
    }
    
    return 0;
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
    std::cout << "Stopping threads" << std::endl;
    m_threadsRunning = false;
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);
    
    if (m_pReassembler) {
	std::cout << "Deregistering worker" << std::endl;
	auto rv = m_pReassembler->deregisterWorker();
	if (rv.has_error()) {
	    std::cerr << "Unable to deregister worker on exit: "
		      << rv.error().message() << std::endl;
	}
	m_pReassembler->stopThreads();
    }
    
    boost::this_thread::sleep_for(duration);
}

FormatSelector::SupportedVersions
Receiver::mapVersion(int vsn)
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

/**
 * @details
 * Typically to be called as part of a monitoring thread. There is no 
 * signaling mechanism between threads: requires m_threadsRunning == true 
 * when the caller thread starts or there is no output.
 */
void
Receiver::statsThread()
{    
    std::vector<boost::tuple<EventNum_t, u_int16_t, size_t>> lostEvents;

    while (m_threadsRunning) {
	auto now = boost::chrono::high_resolution_clock::now();
	
        auto stats = m_pReassembler->getStats();
	
        while(true) {
            auto rvle = m_pReassembler->get_LostEvent();
            if (rvle.has_error()) {
                break;
	    }
            lostEvents.push_back(rvle.value());
        }
	
	/*
	 *  - 0 EventNum_t enqueueLoss;
	 *  - 1 EventNum_t reassemblyLoss;
	 *  - 2 EventNum_t eventSuccess;
	 *  - 3 int lastErrno; 
	 *  - 4 int grpcErrCnt; 
	 *  - 5 int dataErrCnt; 
	 *  - 6 E2SARErrorc lastE2SARError; 
	 */
	std::cout << "Stats:" << std::endl;
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

        auto until = now + boost::chrono::milliseconds(2000);
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
	return -1;
    }

    auto rvrw = m_pReassembler->registerWorker(rvhn.value());
    if (rvrw.has_error()) {
	std::cerr << "Unable to register worker: " << rvrw.error().message()
		  << " with error code " << rvrw.error().code() << std::endl;
        return -1;
    }

    boost::this_thread::sleep_for(boost::chrono::seconds(1));
    
    auto rvoas = m_pReassembler->openAndStart();
    if (rvoas.has_error()) {
     	std::cerr << "Unable to start Reassembler: " << rvoas.error().message()
		  << " with error code " << rvoas.error().code() << std::endl;
	return -1;
    }
    
    return 0;
}

int
Receiver::receiveEvents(DataSink* pSink)
{
    // Received event information and receiver config. The extent of good
    // data for a particular event is defined by evtBufSize.

    u_int8_t*  evtBuf{nullptr}; // Event buffer for data reads
    size_t     evtBufSize;      // Event buffer size in bytes
    EventNum_t evtNum;          // Event number
    u_int16_t  dataId;          // Data source Id
    size_t     totalBytes = 0;  // Total bytes written
    
    auto start = boost::chrono::steady_clock::now();
    
    while (m_threadsRunning) {
	auto rv = m_pReassembler->getEvent(&evtBuf, &evtBufSize,
					   &evtNum, &dataId);

	auto now = boost::chrono::steady_clock::now();
	if (m_duration != 0
	    && (now - start) > boost::chrono::seconds(m_duration)) {
	    break;
	}
	
	if (rv.has_error()) {
	    std::cerr << "Reassembler failed to get event: "
		      << rv.error().message() << " with error code "
		      << rv.error().code() << std::endl;
	    return -1;
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

	write(evtBuf, evtBufSize, pSink);
	
	delete evtBuf;
	evtBuf = nullptr;
    }
    
    return 0;
}

/**
 * @details
 * Creates iovecs of data and calls the sink's `putV()` method to do the 
 * actual write.
 */
void
Receiver::write(void* pData, size_t nBytes, DataSink* pSink)
{
    auto p = static_cast<u_int8_t*>(pData);
    size_t nItems = countRingItems(p, nBytes);
    std::vector<iovec> iovs(nItems);
    
    for (size_t i = 0; i < nItems; i++) {
	iovs[i].iov_base = p;
	iovs[i].iov_len = itemSize(p);
	p = static_cast<u_int8_t*>(nextItem(p));
    }

    pSink->putV(iovs.data(), iovs.size());
}

DataSink*
Receiver::makeDataSink(size_t threadNum)
{
    URL url(makeSinkUri(threadNum));
    std::string proto(url.getProto());
    std::string path(url.getPath());
    if (proto == "tcp" || proto == "ring") {
	return new RingDataSink(path);
    } else if (proto == "file") {
	return new FileDataSink(path);
    } else {
	throw std::runtime_error("Unknown protocol for sink " + proto);
    }
}

std::string
Receiver::makeSinkUri(size_t threadNum)
{
    char uri[1024]; // Hopefully big enough...
    if (m_proto == "ring" || m_proto == "tcp") {
	sprintf(uri, "%s://%s/%s_t%.2d", m_proto.c_str(),
		m_hostName.c_str(), m_baseName.c_str(), threadNum);
    } else if (m_proto == "file") {
	sprintf(uri, "%s://%s/%s_t%.2d.evt", m_proto.c_str(),
		m_basePath.c_str(), m_baseName.c_str(), threadNum);
    }
    
    return std::string(uri);
}

size_t
Receiver::itemSize(void* pData)
{
    return static_cast<RingItemHeader*>(pData)->s_size;
}

void*
Receiver::nextItem(void* pData)
{
    size_t n = itemSize(pData);
    uint8_t* p = static_cast<uint8_t*>(pData);
    p += n;
    
    return p;
}

size_t
Receiver::countRingItems(void* pData, size_t nBytes)
{
    size_t result(0);
    while (nBytes) {
        result++;
        nBytes -= itemSize(pData);
        pData   = nextItem(pData);
    }
    
    return result;
}
