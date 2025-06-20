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
*/

/**
 * @file e2sarwf.cpp
 * @brief Main application for FRIB-E2SAR workflows
 */ 

#include <filesystem>
#include <iostream>

// E2SAR includes and deps:

#include <e2sar.hpp>

// Boost for cmdline, etc.

#include <boost/program_options.hpp>

// NSCLDAQ includes:

#include <Exception.h>

// Project headers:

#include "Sender.h"
#include "Receiver.h"

namespace po = boost::program_options;
using namespace e2sar;

/** @todo (ASC 4/25/25): Alternative to signal-handling to avoid C-style 
 * linkage and singletons. */
/** @todo (ASC 6/6/25): Struct to store variables map options to decouple 
 * Sender and Receiver from boost. */

/**
 * @brief Check if two cmdline options confict
 * @param vm References our variables map
 * @param opt1 First cmdline option
 * @param opt2 Second cmdline option
 * @throw std::logic_error If the two options cannot be used simultaneously
 */
void
conflicting_options(const po::variables_map &vm,
		    const std::string &opt1,
		    const std::string &opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted()
	&& vm.count(opt2) && !vm[opt2].defaulted()) {
        throw std::logic_error(std::string("Conflicting options '")
			       + opt1 + "' and '" + opt2 + "'.");
    }
}

/**
 * @brief Ensure that dependent options are specified
 * @param vm References our variables map
 * @param for_what First cmdline option
 * @param required_option Second cmdline option
 * @throw std::logic_error If a dependency exists but is not satisfied
 */
void option_dependency(const po::variables_map &vm,
		       const std::string &for_what,
		       const std::string &required_option)
{
    if (vm.count(for_what) && !vm[for_what].defaulted())
        if (vm.count(required_option) == 0)
            throw std::logic_error(std::string("Option '") + for_what
				   + "' requires option '"
				   + required_option + "'.");
}

/**
 * @brief Run the application - either in `send` or `recv` mode.
 * @param argc Command-line arguemnt count  
 * @param argv Argument vector
 * @return int
 * @retval EXIT_SUCCESS Success
 * @retval EXIT_FAILURE Failure, generally with some contextual error message
 */
int
main(int argc, char* argv[])
{
    /////////////////////////////////////////////////////////////////////////
    // Configure command line parser
    //
    
    po::options_description od("Command-line options");

    // Base options from e2sar_perf:

    auto opts = od.add_options()("help,h", "show this help message");
    
    opts("send", "send traffic");
    opts("recv", "receive traffic");

    opts("bufsize,b", po::value<size_t>()->default_value(1024*1024),
	 "event buffer size in bytes [s]");
    opts("uri,u", po::value<std::string>()->default_value(""),
	 "specify EJFAT_URI on the command-line instead of the envvar");
    opts("num,n", po::value<size_t>()->default_value(0),
	 "number of event buffers to send (0 send all data) [s]");
    opts("enum,e", po::value<EventNum_t>()->default_value(0),
	 "starting event number [s]");
    opts("srcid", po::value<u_int32_t>()->default_value(0),
	 "event source ID [s]");
    opts("dataid", po::value<u_int16_t>()->default_value(0),
	 "data ID [s]");
    opts("threads,t", po::value<size_t>()->default_value(1),
	 "number of receive threads [r]");
    opts("rate,r", po::value<float>()->default_value(1.0),
	 "send rate in Gbps [s]");
    opts("period,p", po::value<u_int16_t>()->default_value(1000),
	 "receive side reporting thread sleep period in ms [r]");
    opts("duration,d", po::value<int>()->default_value(0),
	 "receiver run duration in seconds (defaults to 0 - until "
	 "Ctrl-C is pressed) [r]");
    opts("ini,i", po::value<std::string>(),
	 "file to initialize SegmenterFlags [s] or ReassemblerFlags [r]. "
	 "Defaults to segmenter_config.ini [s] or reassembler_config.ini [r] "
	 "in current working directory if not provided.");
    opts("ip", po::value<std::string>()->default_value("35.11.82.130"),
	 "IP address (IPv4 or IPv6) from which sender sends from or on which "
	 "receiver listens (conflicts with --autoip) [s,r]");
    opts("port,p", po::value<u_int16_t>()->default_value(20000),
	 "starting UDP port number on which receiver listens. [r] ");
    opts("deq", po::value<size_t>()->default_value(1),
	 "number of event dequeue threads in receiver [r]");
    opts("cores,c", po::value<std::vector<int>>()->multitoken(),
	 "optional list of cores to bind sender or receiver threads to; "
	 "number of receiver threads is equal to the number of cores [s,r]");
    opts("optimize,o", po::value<std::vector<std::string>>()->multitoken(),
	 "a list of optimizations to turn on [s]");
    opts("mtu,m", po::value<u_int16_t>(),
	 "MTU size in bytes) [s]");

    // Additions to base options:
    
    std::string cwd = std::filesystem::current_path();
    opts("nscldaq-version,v", po::value<int>()->default_value(12),
	 "NSCLDAQ data format major version number [s,r]");
    opts("source,s", po::value<std::string>()->default_value(""),
	 "URI data source we're reading from [s]");
    opts("queue-size,q", po::value<size_t>()->default_value(10000),
	 "queue size for recycling send buffers [s]");
    opts("proto", po::value<std::string>()->default_value("ring"),
	 "data sink URI protocol (file or ring) [r]");
    opts("hostname", po::value<std::string>()->default_value("localhost"),
	 "host name for ringbuffer data sink [r]");
    opts("basepath", po::value<std::string>()->default_value(cwd),
	 "base path for file data sink [r]");
    opts("basename", po::value<std::string>()->default_value("reas"),
	 "base name for data sink [r]");    
    opts("verbose", po::value<bool>()->default_value(true),
	 "enable verbose output [s,r]");    
    opts("debug", po::bool_switch()->default_value(false),
	 "enable debugging output [s,r]");
    
    po::variables_map vm;
    
    try {
        po::store(po::parse_command_line(argc, argv, od), vm);
        po::notify(vm);
    }
    catch (const boost::program_options::unknown_option& e) {
	std::cout << "Unable to parse command line: " << e.what() << std::endl;
	return EXIT_FAILURE;
    }

    try {
        conflicting_options(vm, "send", "recv");
        conflicting_options(vm, "send", "threads");
        conflicting_options(vm, "send", "period");
	conflicting_options(vm, "send", "port");
	conflicting_options(vm, "recv", "num");
        conflicting_options(vm, "recv", "enum");
        conflicting_options(vm, "recv", "length");
        conflicting_options(vm, "recv", "src");
        conflicting_options(vm, "recv", "dataid");
        conflicting_options(vm, "recv", "rate");
	conflicting_options(vm, "recv", "queue-size");
	conflicting_options(vm, "recv", "source");
	conflicting_options(vm, "recv", "mtu");
	option_dependency(vm, "send", "ip");
	option_dependency(vm, "recv", "ip");
        option_dependency(vm, "recv", "port");
	option_dependency(vm, "recv", "proto");
	// Non-mandatory program options:
        conflicting_options(vm, "send", "duration");
        conflicting_options(vm, "deq", "send");
        conflicting_options(vm, "cores", "threads");
    }
    catch (const std::logic_error &e) {
        std::cerr << "Error processing command-line options: "
		  << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    if (vm.count("help")) {
        std::cout << od << std::endl;
        return EXIT_SUCCESS;
    }

    /////////////////////////////////////////////////////////////////////////
    // Create application
    //
    
    try {
	if (vm.count("send") || vm.count("recv")) {
	    if (vm.count("send")) {
		Sender sender(vm);
		sender();
	    }
	    if (vm.count("recv")) {
		Receiver receiver(vm);
		receiver();
	    }
	} else {
	    std::cout << od << std::endl;
	}
    }
    catch (std::exception& e) {
	std::cout << "C++ std::exception -- " << e.what() << std::endl;
	return EXIT_FAILURE;
    }
    catch (E2SARException& e) {
	auto msg = static_cast<std::string>(e);
	std::cerr << "E2SAR exception -- " << msg << std::endl;
	return EXIT_FAILURE;
    }
    catch (CException& e) {
	std::cerr << "NSCLDAQ exception -- " << e.ReasonText() << std::endl;
	return EXIT_FAILURE;
    }
    catch (std::string& msg) {
	std::cerr << "std::string exception -- " << msg << std::endl;
	return EXIT_FAILURE;
    }
    catch (...) {
	std::cerr << "Caught unexpected exception, exiting..." << std::endl;
	return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
