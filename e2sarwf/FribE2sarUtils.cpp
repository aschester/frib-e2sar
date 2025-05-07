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
 * @file FribE2sarUtils.cpp
 * @brief Function implementation for E2SAR utils.
 */

#include "FribE2sarUtils.h"

#include <iostream>

///
// This is not my preferred implementation due to the added level of nesting
// but CMake defaults for Doxygen are able to resolve the namespace usage
// in this case but not if the functions are e.g., frib_e2sar::dumpBuffer().
// -ASC 3/12/25
//
namespace frib_e2sar {
    void
    dumpBuffer(u_int8_t* buf, size_t nBytes) {
	size_t printed = 0; // Total bytes printed (incl. padding)
	size_t perLine = 8; // Bytes per line
    
	std::cerr << "-----------------------" << std::endl;
	std::cerr << std::hex;
    
	while (printed < nBytes) {
	    size_t bytesToPrint = std::min(nBytes - printed, perLine);
	    for (size_t i = 0; i < bytesToPrint; i++) {
		std::cerr << std::setw(2) << std::setfill('0')
			  << unsigned(*buf) << " ";
		buf++;
	    }

	    for (size_t i = bytesToPrint; i < perLine; ++i) {
		std::cerr << "   ";
	    }

	    std::cerr << std::endl;
	
	    printed += perLine;
	}
	std::cerr << std::dec << std::endl;
    }

    /** 
     * @details 
     * Failure to create a valid URI is fatal.
     */
    e2sar::EjfatURI
    getUri(const std::string uri, const e2sar::EjfatURI::TokenType& tt,
	   const bool preferV6)
    {
	auto rv = (
	    uri.empty() ?
	    e2sar::EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6) :
	    e2sar::EjfatURI::getFromString(uri, tt, preferV6)
	    );	
	if (rv.has_error()) {
	    std::string msg("Error in parsing URI ");
	    msg += rv.error().message();
	    throw std::runtime_error(msg);
	}
    
	return rv.value();
    }

    e2sar::Segmenter::SegmenterFlags
    getSegmenterFlagsFromFile(std::string fname)
    {
	auto rv = e2sar::Segmenter::SegmenterFlags::getFromINI(fname);
	if (rv.has_error()) {
	    std::string msg("Error reading configuration file: ");
	    msg += rv.error().message();
	    throw std::runtime_error(msg);
	}

	auto flags = rv.value();
    
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

    /**
     * @details
     * The `withLBHeader` flag is set to `!useCP` in this function. 
     * If you attempt to set this flag set in the initialization file, 
     * that value will be ignored.
     */
    e2sar::Reassembler::ReassemblerFlags
    getReassemblerFlagsFromFile(std::string fname)
    {
	auto rv = e2sar::Reassembler::ReassemblerFlags::getFromINI(fname);
	if (rv.has_error()) {
	    std::string msg("Error reading configuration file: ");
	    msg += rv.error().message();
	    throw std::runtime_error(msg);
	}

	auto flags = rv.value();

	// Expect LB header to be included (mainly for testing when
	// useCP == false, as normally LB strips it off in normal operation);
	// value must be !useCP. Set here rather than in the ini file to
	// ensure its correct:
    
	flags.withLBHeader = not flags.useCP;
    
	// Print out some info about the flags:

	std::cout << "Control plane will be "
		  << (flags.useCP ? "ON" : "OFF") << std::endl;
	std::cout << "Expecting LB header "
		  << (flags.withLBHeader ? "YES" : "NO") << std::endl;
	std::cout << (flags.useCP ?
		      "*** Make sure the LB has been reserved and the URI "
		      "reflects the reserved instance information."
		      : "*** Make sure the URI reflects proper data "
		      "address, other parts are ignored.") << std::endl;
    
	return flags;
    }

    void
    printSegmenterFlags(const e2sar::Segmenter::SegmenterFlags& flags)
    {
	std::cout << "Segmenter flags:\n";
	std::cout << "\tdpV6\t\t" << flags.dpV6 << std::endl;
	std::cout << "\tconnectedSocket\t" << flags.connectedSocket
		  << std::endl;
	std::cout << "\tuseCP\t\t" << flags.useCP << std::endl;
	std::cout << "\tzeroRate\t" << flags.zeroRate << std::endl;
	std::cout << "\tusecAsEventNum\t" << flags.usecAsEventNum << std::endl;
	std::cout << "\tsyncPeriodMs\t" << flags.syncPeriodMs << std::endl;
	std::cout << "\tsyncPeriods\t" << flags.syncPeriods << std::endl;
	std::cout << "\tmtu\t\t" << flags.mtu << " (bytes)" << std::endl;
	std::cout << "\tnumSendSockets\t" << flags.numSendSockets << std::endl;
	std::cout << "\tsndSockBufSize\t" << flags.sndSocketBufSize
		  << " (bytes)" << std::endl;
    }

    void
    printReassemblerFlags(const e2sar::Reassembler::ReassemblerFlags& flags)
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
    
} // end namespace frib_e2sar
