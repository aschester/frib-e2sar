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

#ifndef EJFATUTILITIES_H
#define EJFATUTILITIES_H

#include <string>

#include <e2sar.hpp>
#include <e2sarDPSegmenter.hpp>
#include <e2sarDPReassembler.hpp>

/** 
 * @file EjfatUtilities.h
 * @brief Define some useful functions for EJFAT processing.
 * @details
 * Functions exist in the e2sarUtils namespace.
 */

namespace e2sarUtils {
    void dumpBuffer(u_int8_t* buf, size_t nBytes);

    e2sar::EjfatURI getURI(
	const std::string uri, const e2sar::EjfatURI::TokenType& tt,
	const bool preferV6
	);
    e2sar::Segmenter::SegmenterFlags getSegmenterFlagsFromINI(
	std::string fname
	);
    e2sar::Reassembler::ReassemblerFlags getReassemblerFlagsFromINI(
	std::string fname
	);
    void printSegmenterFlags(const e2sar::Segmenter::SegmenterFlags& flags);
    void printReassemblerFlags(
	const e2sar::Reassembler::ReassemblerFlags& flags
	);
}
    
#endif
