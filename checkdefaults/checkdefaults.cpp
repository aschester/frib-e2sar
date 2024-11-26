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
 * @file checkdefaults.cpp
 * @brief Testing E2SAR C++ basic functionality.  
 */

#include <iostream>
#include <tuple>
#include <cstdint>

#include <e2sar.hpp>

using namespace e2sar;

int main(int argc, char* argv[])
{
    // E2SAR software version:

    std::cout << "E2SAR version: " << get_Version() << std::endl;

    // Constant attributes:
    // Note the damn u_int8_t is probably a typedef for unsigned char
    // and ostream is printing ASCII character 5 which is invisible,
    // so we have to convert it before outputting.

    std::cout << "\n-------- Constant attributes --------\n" << std::endl;
    
    std::cout << "Default data plane port: "
	      << DATAPLANE_PORT << std::endl;
    std::cout << "Default Reassembler Header version: "
	      << (int)rehdrVersion << std::endl;
    std::cout << "Default Reassembler Header nibble: "
	      << (int)rehdrVersionNibble << std::endl;
    std::cout << "Default Load Balancer Header version: "
	      << (int)lbhdrVersion << std::endl;
    std::cout << "Default Sync Header version: "
	      << (int)synchdrVersion << std::endl;

    // Hdr lengths:

    std::cout << "IP header length: " << IP_HDRLEN << std::endl;
    std::cout << "DP header length: " << UDP_HDRLEN << std::endl;
    std::cout << "Total header length: " << TOTAL_HDR_LEN << std::endl;
    auto lbreHdrLen = TOTAL_HDR_LEN - IP_HDRLEN - UDP_HDRLEN;
    std::cout << "LB + RE header length: " << lbreHdrLen << std::endl;

    // Set and read Reassembler header fields:

    auto rehdr = REHdr();

    std::cout << "\n-------- Reassembler header --------\n" << std::endl;
    auto reBefore = rehdr.get_Fields();
    std::cout << "Before setting fields: " << reBefore << std::endl;

    rehdr.set(1, 2, 4, 8);
    auto reAfter = rehdr.get_Fields();
    std::cout << "After setting fields: " << reAfter << std::endl;
    std::cout << "Access by member:\n"
	      << "\tdata_id: " << rehdr.get_dataId() << "\n"
	      << "\tbuff_off: " << rehdr.get_bufferOffset() << "\n"
	      << "\tbuff_len: " << rehdr.get_bufferLength() << "\n"
	      << "\tevent_num: " << rehdr.get_eventNum()
	      << std::endl;

    // Set and read Load balancer header fields:

    auto lbhdr = LBHdr();

    std::cout << "\n-------- Load balancer header --------\n" << std::endl;

    auto lbBefore = lbhdr.get_Fields();
    std::cout << "Before setting fields: " << lbBefore << std::endl;
    std::cout << "!!! Note that the first two fields are u_int8_t and are "
	      << "not output correctly." << std::endl;
    std::cout << "Access by member, converting u_int8_t's:\n"
	      << "\tversion: " << (int)lbhdr.get_version() << "\n"
	      << "\tproto: " << (int)lbhdr.get_nextProto() << "\n"
	      << "\tentropy: " << (int)lbhdr.get_entropy() << "\n"
	      << "\tevent_num: " << (int)lbhdr.get_eventNum()
	      << std::endl;

    lbhdr.set(200, 50); // entropy, event_num
    auto lbAfter = lbhdr.get_Fields();
    std::cout << "After setting fields: " << lbAfter << std::endl;
    std::cout << "Access by member, converting u_int8_t's:\n"
	      << "\tversion: " << (int)lbhdr.get_version() << "\n"
	      << "\tproto: " << (int)lbhdr.get_nextProto() << "\n"
	      << "\tentropy: " << (int)lbhdr.get_entropy() << "\n"
	      << "\tevent_num: " << (int)lbhdr.get_eventNum()
	      << std::endl;
    
    return 0;
}
