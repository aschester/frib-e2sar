#ifndef CRECEIVER_H
#define CRECIEVER_H

#include <memory>

#include <boost/program_options.hpp>

#include <NSCLDAQFormatFactorySelector.h>


namespace e2sar {
    class Reassembler;
}
class DataSink;
// namespace ufmt {
//     class RingItemFactoryBase;
// }
namespace po = boost::program_options;

class CReceiver
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
    
    static CReceiver* m_pInstance; // For handling OS signals
    
public:
    CReceiver(po::variables_map& vm);
    ~CReceiver();

    int operator()();

    static void setInstance(CReceiver* r) { m_pInstance = r; };
    static void ctrlCHandler(int sig);

private:
    void shutdown();
    ufmt::FormatSelector::SupportedVersions mapVersion(int vsn);
    void statsThread();
    
    int prepareToReceive();
    int receiveEvents(size_t threadNum);
    void write(void* pData, size_t nBytes, DataSink* pSink);

    DataSink* makeDataSink(size_t threadNum);
    std::string makeSinkUri(size_t threadNum);
    size_t itemSize(void* pData);
    void* nextItem(void* pData);
    size_t countRingItems(void* pData, size_t nBytes);
    
};

#endif
