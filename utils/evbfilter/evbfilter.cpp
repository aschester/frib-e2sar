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
 * @file evbfilter.cpp
 * @brief Filter to strip headers added during `glom --nobuild` of 
 * pre-built event data.
 */

#include <errno.h>
#include <sstream>

#include <boost/program_options.hpp>

// NSCLDAQ:

#include <CBufferedOutput.h>
#include <CRingItem.h>
#include <DataFormat.h>
#include <io.h>
#include <os.h>

// UFMT:

#include <fragment.h>

const unsigned BUFFER_SIZE=1024*1024; //!< Size of output buffer

/** Manager for outputting buffered data, in this case to stdout */
static io::CBufferedOutput outputter(STDOUT_FILENO, BUFFER_SIZE);
static bool debug = false;    //!< Enable debugging output
static uint32_t* pData;       //!< Data buffer
static size_t allocBytes = 0; //!< Bytes allocated to data buffer

namespace po = boost::program_options;
using namespace ufmt::EVB;

/**
 * @brief Read data from stdin and apply the filter if its a PHYSICS_EVENT.
 * @details
 * To ensure data is time-ordered at its final destination of an E2SAR 
 * workflow, we take advantage of the event orderer and glom with the 
 * `--nobuild` option. This ensures time-ordered output of PHYSICS_EVENT data 
 * _but_ with the additional complication that this second ordering stage 
 * means our data looks like:
 * +------------------------+-------------------------- +-----------------+
 * | High-Level Description | Lower-Level Description   | Size (bytes)    |
 * +------------------------+-------------------------- +-----------------+
 * | Built item header      | Ring item header          | 8               |
 * +------------------------+-------------------------- +-----------------+
 * | Built item body header | Ring item body header     | 4 or 20         |
 * +------------------------+-------------------------- +-----------------+
 * | Payload body size      | Number of bytes in body   | 4               |
 * +------------------------+-------------------------- +-----------------+   
 * |                        | Fragment header           | 20              |
 * |                        | [x] Ring item header      | 8               |
 * | Fragment               | [x] Ring item body header | 4 or 20         |
 * |                        | [x] Ring item body        | Determined by   |
 * |                        |                           | Readout program | 
 * +------------------------+-------------------------- +-----------------+
 * where the fragment contains pre-built data. The data we want to parse 
 * out from the event and output are marked with the [x]'s. Since the `glom` 
 * command generating this data is run with the `--nobuild` option, each ring 
 * item contains only a single fragment, which is the built event from the 
 * first event-building stage.
 * @note Processing pipe is `glom | evbfilter | stdintoring`. Dumping data 
 * besides ring items on stdout may lead to undefined behavior.
 * @throw std::runtime_error if data buffer malloc fails
 * @return int
 * @retval 0 Success
 * @retval 1 No data on stdin (also a valid return value)
 */
int
filterItem() {
    int readBytes;
    
    // Every ring item has a header containing the size of the item:
    
    RingItemHeader hdr;
    readBytes = io::readData(STDIN_FILENO, &hdr, sizeof(RingItemHeader));
    if (!readBytes) { return 1; } // Ensure we read something
    if (debug) {
	std::cerr << "--------------------------------------------\n";
	std::cerr << "built item hdr: type " << hdr.s_type
		  << " size " << hdr.s_size << std::endl;
    }
    
    // Whatever is left is the data payload. Allocate new space if needed:
    
    size_t dataBytes = hdr.s_size - sizeof(RingItemHeader);
    if (dataBytes > allocBytes) {
	free(pData);
	pData = (uint32_t*)malloc(dataBytes);
	if (!pData) {
	    std::stringstream msg;
	    msg << "Failed to allocate data buffer size: " << dataBytes;
	    throw std::runtime_error(msg.str());
	} 
	allocBytes = dataBytes;
    }
    readBytes = io::readData(STDIN_FILENO, pData, dataBytes);
    if (!readBytes) { return 1; } // Ensure we read something

    // Apply filter and output data. If its a PHYSICS_EVENT, isolate the
    // original built event, otherwise just output the top-level ring item
    // header and data buffer as-is.

    uint32_t* p = pData; // First word
    
    if (hdr.s_type == PHYSICS_EVENT) {
	// Skip body header, event size, fragment header. Note body header
	// may be empty, in which case its a single uint32_t with a value
	// of either 0 (v11) or sizeof(uint32_t) (v12):
	uint32_t bodyHdrSize = *p;
	if (debug) {
	    if (bodyHdrSize == sizeof(BodyHeader)) {
		BodyHeader* pBodyHdr = reinterpret_cast<BodyHeader*>(p);
		std::cerr << "built body hdr: size " << pBodyHdr->s_size
			  << " ts " << pBodyHdr->s_timestamp
			  << " sid " << pBodyHdr->s_sourceId
			  << " barrier " << pBodyHdr->s_barrier << std::endl;
	    } else {
		std::cerr << "no body header" << std::endl;
	    }
	}
	p += bodyHdrSize/sizeof(uint32_t);
	dataBytes -= bodyHdrSize;
	
	if (debug) {
	    std::cerr << "event size " << *p << std::endl;
	}
	p++;
	dataBytes -= sizeof(uint32_t);

	if (debug) {
	    FragmentHeader* pFragHdr = reinterpret_cast<FragmentHeader*>(p);
	    std::cerr << "fragment hdr: ts " << pFragHdr->s_timestamp
		      << " sid " << pFragHdr->s_sourceId
		      << " size " << pFragHdr->s_size
		      << " barrier " << pFragHdr->s_barrier << std::endl;
	}
	p += sizeof(FragmentHeader)/sizeof(uint32_t);	
	dataBytes -= sizeof(FragmentHeader);
	
	// Ring item header for the fragment to output:

	if (debug) {
	    RingItemHeader* pHdr = reinterpret_cast<RingItemHeader*>(p);
	    std::cerr << "frag item hdr: type " << pHdr->s_type
		      << " size " << pHdr->s_size << std::endl;
	}
	outputter.put(p, sizeof(RingItemHeader));
	p += sizeof(RingItemHeader)/sizeof(uint32_t);
	dataBytes -= sizeof(RingItemHeader);

	bodyHdrSize = *p;
	if (debug) {
	    if (bodyHdrSize = sizeof(BodyHeader)) {	
		BodyHeader* pBodyHdr = reinterpret_cast<BodyHeader*>(p);
		std::cerr << "frag body hdr: size " << pBodyHdr->s_size
			  << " ts " << pBodyHdr->s_timestamp
			  << " sid " << pBodyHdr->s_sourceId
			  << " barrier " << pBodyHdr->s_barrier
			  << std::endl;
	    
	    } else {	    
		std::cerr << "empty frag body hdr" << std::endl;
	    }
	}
	outputter.put(p, bodyHdrSize);
	p += bodyHdrSize/sizeof(uint32_t);
	dataBytes -= bodyHdrSize;
    } else {
	outputter.put(&hdr, sizeof(RingItemHeader));
    }

    outputter.put(p, dataBytes);
    
    return 0;
}

/**
 * @brief Application entry point for filter
 * @param argc Number of command-line args
 * @param argv Argument vector
 * @return EXIT_SUCCESS on success, otherwise EXIT_FAILURE
 */
int
main(int argc, char* argv[])
{
    // Parse args:

    po::options_description od("Command-line options");
    auto opts = od.add_options()("help,h", "show this help message");
    opts("debug", po::bool_switch()->default_value(false),
	 "enable debugging output");
    opts("count,c", po::value<size_t>()->default_value(0),
	 "item count to filter");

    po::variables_map vm;
    
    try {
        po::store(po::parse_command_line(argc, argv, od), vm);
        po::notify(vm);
    }
    catch (const boost::program_options::unknown_option& e) {
	std::cout << "Unable to parse command line: " << e.what() << std::endl;
	return EXIT_FAILURE;
    }

    if (vm.count("help")) {
        std::cout << od << std::endl;
        return EXIT_SUCCESS;
    }

    // Configuration:

    debug = vm["debug"].as<bool>();
    size_t count = vm["count"].as<size_t>();

    if (Os::blockSignal(SIGPIPE)) {
	perror("Failed to block the SIGPIPE: end data may not be flushed");
    }
    
    outputter.setTimeout(2); // Flush every two seconds if rate is low

    // Run event loop:
    
    try {
	size_t ct = 0;
	while (1) {
	    filterItem();	    
	    ct++;
	    if (count && ct == count) {
		break;
	    }
	} // End event loop
    }
    catch (const int& e) {
	std::cerr << "errno error: " << e << ": " << strerror(e) << std::endl;
	free(pData);
	return EXIT_FAILURE;
    }
    catch (const std::runtime_error& e) {
	std::cerr << "runtime error: " << e.what() << std::endl;
	free(pData);
	return EXIT_FAILURE;
    }

    free(pData);
    
    return EXIT_SUCCESS;
}
