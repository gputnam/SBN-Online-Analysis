#include <vector>
#include <iostream>
#include <string>
#include <ctime>

#include "TROOT.h"
#include "TTree.h"

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "canvas/Utilities/InputTag.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h" 
#include "art/Framework/Principal/SubRun.h" 
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include "../ChannelData.hh"
#include "../HeaderData.hh"
#include "../Analysis.hh"
#include "../VSTChannelMap.hh"

#include "Redis.hh"

/*
 * Uses the Analysis class to send stuff to Redis
*/

namespace daqAnalysis {
  class OnlineAnalysis;
}


class daqAnalysis::OnlineAnalysis : public art::EDAnalyzer {
public:
  explicit OnlineAnalysis(fhicl::ParameterSet const & p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  OnlineAnalysis(OnlineAnalysis const &) = delete;
  OnlineAnalysis(OnlineAnalysis &&) = delete;
  OnlineAnalysis & operator = (OnlineAnalysis const &) = delete;
  OnlineAnalysis & operator = (OnlineAnalysis &&) = delete;

  // Required functions.
  void analyze(art::Event const & e) override;

  // finalize to send out sub-run data
  void endJob() override;
private:
  // handle to the channel map service
  art::ServiceHandle<daqAnalysis::VSTChannelMap> _channel_map;

  daqAnalysis::Analysis _analysis;
  daqAnalysis::Redis *_redis_manager;
  bool _config_use_event_time;
};

daqAnalysis::OnlineAnalysis::OnlineAnalysis(fhicl::ParameterSet const & p):
  art::EDAnalyzer::EDAnalyzer(p),
  _channel_map(),
  _analysis(p) 
{
  Redis::Config config;
  // config for redis
  config.stream_take = p.get<std::vector<unsigned>>("stream_take");
  config.stream_expire = p.get<std::vector<unsigned>>("stream_expire");
  config.snapshot_time = p.get<int>("snapshot_time", -1);
  config.hostname = p.get<std::string>("hostname", "127.0.0.1");
  config.sub_run_stream = p.get<bool>("sub_run_stream", false);
  config.sub_run_stream_expire = p.get<unsigned>("sub_run_expire", 0);
  config.first_subrun = p.get<unsigned>("first_subrun", 0);
  config.monitor_name = p.get<std::string>("monitor_name", "");
  config.flush_data = p.get<bool>("flush_data", true);
  config.print_data = p.get<bool>("print_data", false);
  
  // have Redis alloc fft if you don't calculate them and you know the input size
  config.waveform_input_size = (!_analysis._config.fft_per_channel && _analysis._config.static_input_size > 0) ?
    _analysis._config.static_input_size : -1;

  config.timing = _analysis._config.timing;

  // setup redis
  _redis_manager = new Redis(config, _channel_map.get());

  // config for online analysis module
  _config_use_event_time = p.get<bool>("use_event_time", false);
}

void daqAnalysis::OnlineAnalysis::analyze(art::Event const & e) {

  _analysis.AnalyzeEvent(e);

  auto const& raw_digits_handle = e.getValidHandle<std::vector<raw::RawDigit>>(_analysis._config.daq_tag);
  unsigned sub_run = e.subRun();
  unsigned run = e.run();

  if (_analysis.ReadyToProcess() && !_analysis.EmptyEvent()) {
    // if configured to, get the time from the event
    if (_config_use_event_time) {
      //std::cout << "TIME HIGH: " << e.time().timeHigh() << std::endl;
      //std::cout << "TIME LOW: " << e.time().timeLow() << std::endl;
      //std::cout << "TIME VAL: " << e.time().value() << std::endl;
      _redis_manager->StartSend(e.time().timeLow(), run, sub_run);
    }
    // else use default time (now)
    else {
      _redis_manager->StartSend(run, sub_run);
    }

    // sum waveforms if we're gonna take a snapshot
    if (_redis_manager->WillTakeSnapshot()) {
      _analysis.SumWaveforms(e);
    }

    _redis_manager->ChannelData(&_analysis._per_channel_data, &_analysis._noise_samples, &_analysis._fem_summed_waveforms, 
        &_analysis._fem_summed_fft, raw_digits_handle, _analysis._channel_index_map);
    // send headers if _analysis was configured to copy them

    _redis_manager->EventInfo(&_analysis._event_info);

    if (_analysis._config.n_headers > 0) {
        _redis_manager->HeaderData(&_analysis._header_data);
    }
    _redis_manager->FinishSend();
  }
}

void daqAnalysis::OnlineAnalysis::endJob() {
   // flush the reamining data in redis
   _redis_manager->FlushData();
}

DEFINE_ART_MODULE(daqAnalysis::OnlineAnalysis)
