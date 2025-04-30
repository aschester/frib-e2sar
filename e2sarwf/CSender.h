#ifndef CSENDER_H
#define CSENDER_H

#include <string>
#include <memory>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/lockfree/lockfree_forward.hpp>

#include <NSCLDAQFormatFactorySelector.h>

namespace e2sar {
    class Segmenter;
    class LBManager;
}
class DataSource;
namespace ufmt {
    class RingItemFactoryBase;
}
namespace po = boost::program_options;

class CSender
{    
private:
    float m_rateGbps;
    u_int16_t m_dataId;
    size_t m_nEvents;
    size_t m_evtBufSize;
    size_t m_maxBufBytes;
    bool m_threadsRunning;
    bool m_debug;
    bool m_verbose;
    std::vector<std::string> m_senders;
    
    std::unique_ptr<e2sar::Segmenter> m_pSegmenter;
    std::unique_ptr<e2sar::LBManager> m_pLBManager;
    std::unique_ptr<DataSource> m_pSource;
    std::unique_ptr<boost::lockfree::queue<u_int8_t*>> m_pEvtBufQueue;

    static CSender* m_pInstance; // For handling OS signals
    
public:
    CSender(po::variables_map& vm);
    ~CSender();

    int operator()();

    static void setInstance(CSender* s) { m_pInstance = s; };
    static void ctrlCHandler(int sig);

private:
    void shutdown();
    ufmt::FormatSelector::SupportedVersions mapVersion(int vsn);
    DataSource* makeDataSource(ufmt::RingItemFactoryBase*, const std::string& strUrl);
    };

#endif
