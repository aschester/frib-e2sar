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
 * @file FribEjfatUtils.h
 * @brief Define some useful functions for E2SAR processing
 * @details
 * Functions exist in the frib_e2sar namespace
 */

#ifndef FRIBE2SARUTILS_H
#define FRIBE2SARUTILS_H

#include <string>

#include <e2sar.hpp>

/**
 * @namespace frib_e2sar
 * @brief Namespace for utility functions
 */
namespace frib_e2sar {
    /**
     * @brief Byte dump of buffer to stderr.
     * @param buf Pointer to the start of the data buffer we're dumping
     * @param nBytes Number of bytes to dump
     */    
    void dumpBuffer(u_int8_t* buf, size_t nBytes);
    /**
     * @brief Read URI from EJFAT_URI or non-empty string if passed.
     * @param uri URI string; if empty, read from EJFAT_URI environment 
     * variable.
     * @param tt Token type used to construct the URI
     * @param preferV6 Prefer IpV6 (optional, default=false)
     * @throw std::runtime_error If the URI creation fails
     * @return The Ejfat URI
     */
    e2sar::EjfatURI getUri(const std::string uri,
			   const e2sar::EjfatURI::TokenType& tt,
			   const bool preferV6=false);
    /**
     * @brief Read segmenter configuration from ini file.
     * @param fname Name of the configuration file for the segmenter
     * @throw std::runtime_error If we cannot read the flags from the ini file 
     * @return The segmenter flags read from the file
     */
    e2sar::Segmenter::SegmenterFlags getSegmenterFlagsFromFile(std::string fname);
    /**
     * @brief Read reassembler configuration from .ini file.
     * @param fname Name of the configuration file for the reassembler
     * @throw std::runtime_error If we cannot read the flags from the ini file
     * @return The reassembler flags read from the file
     */
    e2sar::Reassembler::ReassemblerFlags getReassemblerFlagsFromFile(std::string fname);
    /**
     * @brief Print the segmenter flags to stdout.
     * @param flags The flags
     */
    void printSegmenterFlags(const e2sar::Segmenter::SegmenterFlags& flags);
    /**
     * @brief Print the reassembler flags to stdout.
     * @param flags The flags
     */
    void printReassemblerFlags(const e2sar::Reassembler::ReassemblerFlags& flags);
}
    
#endif
