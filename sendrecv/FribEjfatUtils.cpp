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
 * @file FribEjfatUtils.cpp
 * @brief Function implementation for EJFAT utils.
 */

#include "FribEjfatUtils.h"

#include <iostream>

using namespace e2sar;

/**
 * @brief Dump the buffer to stderr.
 * @param buf Pointer to the start of the data buffer we're dumping
 * @param nBytes Number of bytes to dump
 */
void
frib_ejfat::dumpBuffer(u_int8_t* buf, size_t nBytes) {
    std::cerr << "------------------------------------------" << std::endl;
    std::cerr << std::hex;
    for (size_t i = 0; i < nBytes; i++) {
	for (int j = 0; j < 8; j++) {
	    if (i+1 >= nBytes) break;
	    std::cerr << *buf << " ";
	    buf++;
	}
	std::cerr << std::endl;
    }
    std::cerr << std::dec << std::endl;
}

/**
 * @brief Read URI from EJFAT_URI or non-empty string if passed.
 * @param uri URI string; if empty, read from EJFAT_URI environment variable.
 * @param tt  Token type used to construct the URI
 * @param preferV6 Prefer IpV6 (optional, default=false)
 * @return The Ejfat URI
 * @note Failure to create a valid URI is fatal
 */
EjfatURI
frib_ejfat::getURI(const std::string uri, const EjfatURI::TokenType& tt,
		   const bool preferV6=false)
{
    auto uri_rv = (
	uri.empty() ?
	EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6) :
	EjfatURI::getFromString(uri, tt, preferV6)
	);	
    if (uri_rv.has_error())
    {
	std::cerr << "Error in parsing URI "s + uri_rv.error().message()
		  << std::endl;
	exit(EXIT_FAILURE);
    }
    
    return uri_rv.value();
}

/**
 * @brief Read segmenter configuration from INI file.
 * @param fname Name of the configuration file for the segmenter
 * @return The segmenter flags read from the file
 */
Segmenter::SegmenterFlags
frib_ejfat::getSegmenterFlagsFromINI(std::string fname)
{
    auto flags_rv = Segmenter::SegmenterFlags::getFromINI(fname);
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

/**
 * @brief Read reassembler configuration from INI file.
 * @param fname Name of the configuration file for the reassembler
 * @return The reassembler flags read from the file
 */
Reassembler::ReassemblerFlags
frib_ejfat::getReassemblerFlagsFromINI(std::string fname)
{
    auto flags_rv = Reassembler::ReassemblerFlags::getFromINI(fname);
    if (flags_rv.has_error()) {
	std::cerr << "Error reading configuration file: "s
	    + flags_rv.error().message() << std::endl;
	exit(EXIT_FAILURE);
    }

    auto flags = flags_rv.value();

    // Expect LB header to be included (mainly for testing when useCP == false, 
    // as normally LB strips it off in normal operation); value must be !useCP.
    // Set here rather than in the ini file to ensure its correct:
    
    flags.withLBHeader = not flags.useCP;
    
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

/**
 * @brief Print the segmenter flags to stdout.
 * @param flags The flags
 */
void
frib_ejfat::printSegmenterFlags(const Segmenter::SegmenterFlags& flags)
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

/**
 * @brief Print the reassembler flags to stdout.
 * @param flags The flags
 */
void
frib_ejfat::printReassemblerFlags(const Reassembler::ReassemblerFlags& flags)
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
