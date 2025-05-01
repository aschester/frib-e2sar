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

#ifndef RECEIVER_H
#define RECIEVER_H

#include <memory>

#include <boost/program_options.hpp>

#include <NSCLDAQFormatFactorySelector.h>

namespace e2sar {
    class Reassembler;
}
class DataSink;
namespace po = boost::program_options;

class Receiver
{
private:
    std::string m_proto;
    std::string m_hostName;
    std::string m_basePath;
    std::string m_baseName;
    int m_duration;
    size_t m_deqThreads;
    bool m_threadsRunning;
    bool m_debug;
    bool m_verbose;

    std::unique_ptr<e2sar::Reassembler> m_pReassembler;
    
    static Receiver* m_pInstance; // For handling OS signals
    
public:
    Receiver(po::variables_map& vm);
    ~Receiver();

    int operator()();

    static void setInstance(Receiver* r) { m_pInstance = r; };
    static void ctrlCHandler(int sig);

private:
    void shutdown();
    ufmt::FormatSelector::SupportedVersions mapVersion(int vsn);
    void statsThread();    
    int prepareToReceive();
    int receiveEvents(size_t threadNum, DataSink* pSink);
    void write(void* pData, size_t nBytes, DataSink* pSink);
    DataSink* makeDataSink(size_t threadNum);
    std::string makeSinkUri(size_t threadNum);
    size_t itemSize(void* pData);
    void* nextItem(void* pData);
    size_t countRingItems(void* pData, size_t nBytes);
};

#endif
