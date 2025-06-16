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
 * @brief Filter to strip extra header from data processed using 
 * `glom --nobuild`
 */

#include <errno.h>

#include <boost/program_options.hpp>

#include <CBufferedOutput.h>
#include <CRingItem.h>
#include <DataFormat.h>
#include <io.h>
#include <os.h>

#include <fragment.h>

const unsigned BUFFER_SIZE=1024*1024;

static io::CBufferedOutput outputter(STDOUT_FILENO, BUFFER_SIZE);
static bool debug = false;
uint32_t* pData = nullptr;
size_t allocBytes = 0;

namespace po = boost::program_options;
using namespace ufmt::EVB;

/**
 * @brief Read data from stdin, determine the ring item type and apply filter.
 * @details
 * To ensure data is time-ordered at its final destination of an E2SAR 
 * workflow, we take advantage of the event orderer and glom with the 
 * `--nobuild` option. This ensures time-ordered output of PHYSICS_EVENT data 
 * _but_ with the additional complication that this second ordering stage 
 * means our data looks like:
 * +------------------------+--------------------------+-----------------+
 * | High-Level Description | Lower-Level Description  | Size (bytes)    |
 * +------------------------+--------------------------+-----------------+
 * | Header                 | Ring item header         | 8               |
 * +------------------------+--------------------------+-----------------+
 * | Body header            | Ring item body header    | 4 or 20         |
 * +------------------------+--------------------------+-----------------+
 * +------------------------+--------------------------+-----------------+
 * | Body size              | Number of bytes in body  | 4               |
 * +------------------------+--------------------------+-----------------+   
 * |                        | Fragment header          | 20              |
 * |                        | Ring item header         | 8               |
 * | Fragment #0            | Ring item body header    | 4 or 20         |
 * |                        | Ring item body           | Determined by   |
 * |                        |                          | Readout program | 
 * +------------------------+--------------------------+-----------------+
 * where Fragment #0's ring item body is a complete built event. Because this 
 * data was produced without the event builder correlating events, it contains
 * only a single fragment.
 * @note The expected pipe here is glom | evbfilter | stdintoring so it is 
 * imperitive that nothing else is put on stdout within this function. 
 * All debugging output _must_ go on stderr or data will be malformed in the 
 * ringbuffer!
 * @throw std::runtime_error if data buffer malloc fails
 * @return int
 * @retval 0 Success
 * @retval 1 No data on stdin
 */
int
filterItem() {
    int nread;
    
    // Every ring item has a header:
    
    RingItemHeader hdr;
    nread = io::readData(STDIN_FILENO, &hdr, sizeof(RingItemHeader));
    if (!nread) { // Ensure we actually read something
	return 1;
    }
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
	allocBytes = dataBytes;
	if (!pData) {
	    throw std::runtime_error("Failed to allocate data buffer!");
	}
    }
    nread = io::readData(STDIN_FILENO, pData, dataBytes);
    if (!nread) {
	return 1;
    }

    // Apply filter and output data. If its a PHYSCIS_EVENT, isolate the
    // original built event, otherwise just output the top-level ring item
    // header and data buffer as-is.

    uint32_t* p = pData; // First word
    
    if (hdr.s_type == PHYSICS_EVENT) {
	// Skip body header, event size, fragment header:
	if (debug) {
	    BodyHeader* pBodyHdr = reinterpret_cast<BodyHeader*>(p);
	    std::cerr << "built body hdr: size " << pBodyHdr->s_size
		      << " ts " << pBodyHdr->s_timestamp
		      << " sid " << pBodyHdr->s_sourceId
		      << " barrier " << pBodyHdr->s_barrier << std::endl;
	}
	p += sizeof(BodyHeader)/sizeof(uint32_t);
	dataBytes -= sizeof(BodyHeader);
	
	if (debug) {
	    uint32_t eventSize = *p;
	    std::cerr << "event size " << eventSize << std::endl;
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

	RingItemHeader* pOrigHdr = reinterpret_cast<RingItemHeader*>(p);
	if (debug) {
	    std::cerr << "frag item hdr: type " << pOrigHdr->s_type
		      << " size " << pOrigHdr->s_size << std::endl;
	}
	p += sizeof(RingItemHeader)/sizeof(uint32_t);
	dataBytes -= sizeof(RingItemHeader);

	BodyHeader* pOrigBodyHdr = reinterpret_cast<BodyHeader*>(p);
	if (debug) {
	    std::cerr << "frag body hdr: size " << pOrigBodyHdr->s_size
		      << " ts " << pOrigBodyHdr->s_timestamp
		      << " sid " << pOrigBodyHdr->s_sourceId
		      << " barrier " << pOrigBodyHdr->s_barrier << std::endl;
	}
	p += sizeof(BodyHeader)/sizeof(uint32_t);
	dataBytes -= sizeof(BodyHeader);
	
	outputter.put(pOrigHdr, sizeof(RingItemHeader));
	outputter.put(pOrigBodyHdr, sizeof(BodyHeader));	
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
	return EXIT_FAILURE;
    }
    catch (const std::runtime_error& e) {
	std::cerr << "runtime error: " << e.what() << std::endl;
	return EXIT_FAILURE;
    }

    free(pData);
    
    return EXIT_SUCCESS;
}
