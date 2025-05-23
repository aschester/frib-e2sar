/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2017.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Authors:
             Ron Fox
             Giordano Cerriza
	     Aaron Chester
	     FRIB
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

/** 
 * @file tsvalidate.cpp
 * @brief Validate timestamp ordering for E2SAR pipeline processing. 
 * This is the ufmt evtdump modified to validate that event timestamps 
 * are monotonically increasing.
 * @note (ASC 5/22/25): Issue with CRangError exception when attempting to
 * attach to ringbuffer when rate is high ( > 5 Gbps or so).
 */

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

// Unified format library headers:

#include <NSCLDAQFormatFactorySelector.h>
#include <RingItemFactoryBase.h>
#include <DataFormat.h>
#include <CRingItem.h>
#include <CAbnormalEndItem.h>
#include <CDataFormatItem.h>
#include <CGlomParameters.h>
#include <CPhysicsEventItem.h>
#include <CRingFragmentItem.h>
#include <CRingPhysicsEventCountItem.h>
#include <CRingScalerItem.h>
#include <CRingTextItem.h>
#include <CRingStateChangeItem.h>
#include <CUnknownFragment.h>
#include <fmtconfig.h>

// Other NSCLDAQ headers:

#include <CRemoteAccess.h>
#include <CRingBuffer.h>
#include <Exception.h>
#include <RangeError.h>
#include <URL.h>

// Project headers:

#include <DataSource.h>
#include <FdDataSource.h>
#include <StreamDataSource.h>
#include <RingDataSource.h>

#include "cmdline.h"

using namespace ufmt;

uint64_t currentTs = 0; //!< Timestamp of current event
uint64_t prevTs = 0;    //!< Timestamp of previous event
uint64_t counter = 0;   //!< Fragment counter

// Map of exclusion type strings to type integers:

static std::map<std::string, uint32_t> TypeMap = {
    {"BEGIN_RUN", BEGIN_RUN},
    {"END_RUN", END_RUN},
    {"PAUSE_RUN", PAUSE_RUN},
    {"RESUME_RUN", RESUME_RUN},
    {"ABNORMAL_ENDRUN", ABNORMAL_ENDRUN},
    {"PACKET_TYPES", PACKET_TYPES},
    {"MONITORED_VARIABLES", MONITORED_VARIABLES},
    {"RING_FORMAT", RING_FORMAT},
    {"PERIODIC_SCALERS", PERIODIC_SCALERS},
    {"INCREMENTAL_SCALERS", INCREMENTAL_SCALERS},
    {"TIMESTAMPED_NONINCR_SCALERS", TIMESTAMPED_NONINCR_SCALERS},
    {"PHYSICS_EVENT", PHYSICS_EVENT},
    {"PHYSICS_EVENT_COUNT", PHYSICS_EVENT_COUNT},
    {"EVB_FRAGMENT", EVB_FRAGMENT},
    {"EVB_UNKNOWN_PAYLOAD", EVB_UNKNOWN_PAYLOAD},
    {"EVB_GLOM_INFO", EVB_GLOM_INFO}
};

/**
 * @brief Tokenize (split) a string. Shamelessly stolen from 
 * https://www.techiedelight.com/split-string-cpp-using-delimiter/
 * @param str String to split up.
 * @param delim Delimimeter on which to split the string.
 * @return Vector of tokenized strings
 */
static std::vector<std::string>
tokenize(std::string const &str, const char delim)
{
    std::vector<std::string> out;
    size_t start;
    size_t end = 0;
 
    while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
    {
        end = str.find(delim, start);
        out.push_back(str.substr(start, end - start));
    }
    
    return out;
}

/**
 * @brief Process a ring item. If the item is a physics event, extract the 
 * timestamp and compare it to the previous event's timestamp. Complain if 
 * the times are not increasing. All other item types are ignored except
 * the ring format item, which will throw if the wrong format is provided.
 * @param pItem Pointer to the item
 * @param factory Reference to the factory appropriate to the format
 * @throw std::logic_error The factory version and ring item format differ
 */
static void
processItem(CRingItem* pItem, RingItemFactoryBase& factory)
{
    // Note that the switch statement here assumes that if you have a ring
    // item type the factory can generate it... this fails if the wrong
    // version of the factory is used for the event file.
    
    switch(pItem->type()) {
    case BEGIN_RUN:
    {
	std::cout << "Begin run found, resetting counter..." << std::endl;
	counter = 0;
    }
    break;
    case RING_FORMAT:
    {
	try {
	    std::unique_ptr<CDataFormatItem> p(
		factory.makeDataFormatItem(*pItem)
		);
	}
	catch (std::bad_cast e) {
	    throw std::logic_error("Unable to dump a data format item... "
				   "likely you've specified the wrong "
				   "--format");
	}
    }
    break;
    case PHYSICS_EVENT:
    {
	std::unique_ptr<CPhysicsEventItem> p(
	    factory.makePhysicsEventItem(*pItem)
	    );
	currentTs = p->getEventTimestamp();
	if (currentTs <= prevTs) {
	    std::cerr << "Timestamps not increasing!!! Fragment number "
		      << counter << " current: " << currentTs
		      << " prev: " << prevTs << std::endl;
	}
	prevTs = currentTs;
    }
    break;
    default:
	break;
    }

    counter++;
}
    
/**
 * @brief Creates a vector of the ring item types to be excluded from the dump
 * given a comma separated list of types. A type can be a string or a positive 
 * number. If it is a string, it is translated to the type id using TypeMap.
 * If it is a number, it is used as is.
 * @param exclusions String containing the exclusion list.
 * @return Vector of items to exclude.
 * @throw std::invalid_argument An exclusion item is not a string and is not 
 * in the map of recognized item types.                
 */
std::vector<uint32_t>
makeExclusionList(const std::string& exclusions)
{
    std::vector<uint32_t> result;
    std::vector<std::string> words = tokenize(exclusions, ',');
    
    // Process the words into an exclusion list:
    
    for (auto s : words) {
        bool isInt(true);
        int intValue;
        try {
            intValue = std::stoi(s);
        }
        catch (...) {
            isInt = false;
        }
        if (isInt) {
            result.push_back(intValue);
        } else {
            auto p = TypeMap.find(s);
            if (p != TypeMap.end()) {
                result.push_back(p->second);
            } else {
                std::string msg("Invalid item type in exclusion list: ");
                msg += s;
                throw std::invalid_argument(msg);
            }
        }
    }
    
    return result;
}

/**
 * @brief Here we:
 * - Parse the URI of the source
 * - Create the underlying connection: stream, fd, ringbuffer
 * - Create the correcte concrete instance of DataSource given all that
 * @param pFactory Pointer to the ring item factory to use
 * @param strUrl String URI of the connection
 * @return Pointer to dynamically allocated data source
 * @throw std::exception Derived exception on failure, which can come from
 * not being able to form the underlying connection
 */
DataSource*
makeDataSource(RingItemFactoryBase* pFactory, const std::string& strUrl)
{
    // Special case the url is just "-"  then it's stdin, a file descriptor
    // data source:
    
    if (strUrl == "-") {
        return new FdDataSource(pFactory, STDIN_FILENO);   
    }
    
    // Parse the URI:
    
    URL uri(strUrl);
    std::string protocol = uri.getProto();
    
    if ((protocol == "tcp") || (protocol == "ring")) {
        try {
            CRingBuffer* pRing = CRingAccess::daqConsumeFrom(strUrl);
            return new RingDataSource(pFactory, *pRing);
        }
        catch (CException& e) {
            throw std::invalid_argument(e.ReasonText());
        }
    } else {
        std::string path = uri.getPath();
	// Need it to last past block:
        std::ifstream& in(*(new std::ifstream(path.c_str()))); 
        return new StreamDataSource(pFactory, in);
    }
}

/**
 * @brief Map the version we get from the command line to a factory version
 * @param fmtIn Format the user requested
 * @return FormatSelector::SupportedVersions - Factory version id
 * @throw std::invalid_argument Bad format version
 * @note We should never throw because gengetopt will enforce the enum.
 */
static FormatSelector::SupportedVersions
mapVersion(enum_format fmtIn)
{
    switch (fmtIn) {
    case format_arg_v12:
	return FormatSelector::v12;
    case format_arg_v11:
	return FormatSelector::v11;
    case format_arg_v10:
	return FormatSelector::v10;
    default:
	throw std::invalid_argument("Invalid DAQ format version specifier");
    }
}

/**
 * @brief  Given a source string URI creates the actual one. In this case it's 
 * a matter of mapping "" into tcp://localhost/username.
 * @param srcIn Source provided by user (or not)
 * @return Actual source string
 * @note `-` is also used as a data source: it means stdin
 */
static std::string
makeSourceString(const char* srcIn)
{
    std::string result(srcIn);
    if (srcIn == "") {
        std::string user(getlogin());
        result = "tcp://localhost/";
        result += user;
    }
    
    return result;
}

/**
 * @brief Main processing loops
 * @details Invalid timestamp information is printed to stderr
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success
 */
int main(int argc, char** argv)
{
    try {
        gengetopt_args_info args;
        cmdline_parser(argc , argv, &args);
        
        // Figure out the parameters:
        
        std::string dataSource = makeSourceString(args.source_arg);
        int skipCount = args.skip_given ? args.skip_arg : 0;
        int dumpCount = args.count_arg;
        std::string excludeItems = args.exclude_arg;
        std::vector<uint32_t> exclusionList = makeExclusionList(excludeItems);
        int scalerBits = args.scaler_width_arg;
        auto defaultVersion = mapVersion(args.format_arg);
        auto& fact = FormatSelector::selectFactory(defaultVersion);
        
        // Now we need to take the URI and the factory and create a source:
        
        std::unique_ptr<DataSource> pSource(makeDataSource(&fact, dataSource));

	// Parse into scaler mask:
	
        uint64_t sbits = 1;
        sbits = sbits << scalerBits;
        sbits--;
        ::CRingScalerItem::m_ScalerFormatMask = sbits;
        
        // If there's a skip count skip exactly that many items:
        
        if (skipCount > 0) {
            for (int i = 0; i < skipCount; i++) {
                std::unique_ptr<CRingItem> p(pSource->getItem());
                if (!p.get()) {
                    exit(EXIT_SUCCESS);
                }
            }
        }
	
        // Now dump the items that are not excluded and if there's a dumpCount
        // only dump that many items -- or until the end of the data source:

	std::cout << "Checking event timestamps..." << std::endl;
	
        int remaining = dumpCount;
        while(1) {
            std::unique_ptr<CRingItem> pItem(pSource->getItem());
            if (!pItem.get()) {
		break;
            }
            
            if (std::find(
		    exclusionList.begin(), exclusionList.end(), pItem->type()
		    ) == exclusionList.end()) {
		
                // Dumpable:
                    
                processItem(pItem.get(), fact);
                
                // Apply any limit to the count:
                
                if (args.count_given) {
                    remaining--;
                    if(remaining <= 0) {
			break;
                    }
                }
            }
	}
	std::cout << "... Done!" << std::endl;
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        cmdline_parser_print_help();
        std::exit(EXIT_FAILURE);
    }
    catch (CRangeError& e) {
	std::cerr << e.ReasonText() << std::endl;
	std::exit(EXIT_FAILURE);
    }
    
    return 0;
}
