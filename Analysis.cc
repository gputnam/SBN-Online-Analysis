//some standard C++ includes
#include <iostream>
#include <stdlib.h>
#include <string>
#include <vector>
#include <numeric>
#include <getopt.h>
#include <chrono>
#include <float.h>

//some ROOT includes
#include "TInterpreter.h"
#include "TROOT.h"
#include "TH1F.h"
#include "TTree.h"
#include "TFile.h"
#include "TStyle.h"
#include "TSystem.h"
#include "TGraph.h"
#include "TFFTReal.h"

// art includes
#include "canvas/Utilities/InputTag.h"
//#include "canvas/Persistency/Common/FindMany.h"
//#include "canvas/Persistency/Common/FindOne.h"
#include "canvas/Persistency/Common/FindOneP.h"
#include "canvas/Persistency/Common/FindManyP.h"
#include "canvas/Persistency/Common/Ptr.h"
#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h" 
#include "art/Framework/Principal/SubRun.h" 
#include "fhiclcpp/ParameterSet.h" 
#include "messagefacility/MessageLogger/MessageLogger.h"  
#include "art/Framework/Services/Optional/TFileService.h"
#include "lardataobj/RawData/RawDigit.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RawData/raw.h"
#include "canvas/Persistency/Provenance/Timestamp.h"

#include "Analysis.hh"
#include "ChannelData.hh"
#include "VSTChannelMap.hh"
#include "HeaderData.hh"
#include "FFT.hh"
#include "Noise.hh"
#include "PeakFinder.hh"
#include "Mode.hh"
#include "Purity.hh"
#include "EventInfo.hh"

using namespace daqAnalysis;

Analysis::Analysis(fhicl::ParameterSet const & p) :
  _channel_map(), // get a handle to the VST Channel Map service
  //art::ServiceHandle<daqAnalysis::VSTChannelMap>()->GetProvider()), // get a handle to the VST Channel Map service
  _config(p),
  _channel_index_map(_channel_map->NChannels()),
  _per_channel_data(_channel_map->NChannels()),
  _per_channel_data_reduced((_config.reduce_data) ? _channel_map->NChannels() : 0), // setup reduced event vector if we need it
  _noise_samples(_channel_map->NChannels()),
  _header_data(std::max(_config.n_headers,0)),
  _event_info(),
  _nevis_tpc_metadata(std::max(_config.n_metadata,0)),
  _thresholds( (_config.threshold_calc == 3) ? _channel_map->NChannels() : 0),
  _fem_summed_waveforms((_config.sum_waveforms) ? _channel_map->NFEM() : 0),
  _fem_summed_fft((_config.sum_waveforms && _config.fft_summed_waveforms) ? _channel_map->NFEM() : 0),
  _fft_manager(  (_config.static_input_size > 0) ? _config.static_input_size: 0),
  _analyzed(false)

{
  _event_ind = 0;
  _sub_run_start_time = -99999;
  _sub_run_holder = -99999;
}

Analysis::AnalysisConfig::AnalysisConfig(const fhicl::ParameterSet &param) {
  // set up config

  // whether to print stuff
  verbose = param.get<bool>("verbose", false);
  // number of events to take in before exiting
  // will never exit if set to negative
  // Also--currently does nothing.
  n_events = param.get<unsigned>("n_events", -1);

  // configuring analysis code:

  // thresholds for peak finding
  // 0 == use static threshold
  // 1 == use gauss fitter rms
  // 2 == use raw rms
  // 3 == use rolling average of rms
  threshold_calc = param.get<unsigned>("threshold_calc", 0);
  threshold_sigma = param.get<float>("threshold_sigma", 5.);
  threshold = param.get<float>("threshold", 100);

  // determine method to get noise sample
  // 0 == use first `n_noise_samples`
  // 1 == use peakfinding
  noise_range_sampling = param.get<unsigned>("noise_range_sampling",0);

  // whether to use plane data in peakfinding
  use_planes = param.get<bool>("use_planes", false);

  // method to calculate baseline:
  // 0 == assume baseline is 0
  // 1 == assume baseline is in digits.GetPedestal()
  // 2 == use mode finding to get baseline
  baseline_calc = param.get<unsigned>("baseline_calc", 1);

  // whether to refine the baseline by calculating the mean
  // of all adc values
  refine_baseline = param.get<bool>("refine_baseline", false);

  // sets percentage of mode samples to be 100 / n_mode_skip
  n_mode_skip = param.get<unsigned>("n_mode_skip", 1);
 
  // only used if noise_range_sampling == 0
  // number of samples in noise sample
  n_noise_samples = param.get<unsigned>("n_noise_samples", 20);

  // number of samples to average in each direction for peak finding
  n_smoothing_samples = param.get<unsigned>("n_smoothing_samples", 1);
  // number of consecutive samples that must be above threshold to count as a peak
  n_above_threshold = param.get<unsigned>("n_above_threshold", 1);

  // Number of input adc counts per waveform. Set to negative if unknown.
  // Setting to some positive number will speed up FFT's.
  static_input_size = param.get<int>("static_input_size", -1);
  // how many headers to expect (set to negative if don't process) 
  n_headers = param.get<int>("n_headers", -1);
  // how many metadata to expect (set to negative if don't process) 
  n_metadata = param.get<int>("n_metadata", -1);

  // whether to calculate/save certain things
  sum_waveforms = param.get<bool>("sum_waveforms", false);
  fft_summed_waveforms = param.get<bool>("fft_summed_waveforms", false);
  fft_per_channel = param.get<bool>("fft_per_channel", false);
  fill_waveforms = param.get<bool>("fill_waveforms", false);
  reduce_data = param.get<bool>("reduce_data", false);
  timing = param.get<bool>("timing", false);

  // name of producer of raw::RawDigits
  std::string producer = param.get<std::string>("producer_name");
  daq_tag = art::InputTag(producer, "");

  //HitFinderName                                                               
  fHitsModuleLabel = param.get<std::string>("HitsModuleLabel","RawHitFinder");

  // Use Hits rather than peak finder                                            
  fUseRawHits = param.get<bool>("UseRawHits", false);
  // Whether to process output from RawHitFinder
  fProcessRawHits = param.get<bool>("ProcessRawHits", false);

  // Muon Triggering Bools
  fUseNevisClock = param.get<bool>("UseNevisClock", false);
  fDoPurityAna = param.get<bool>("DoPurityAna", true);
  fCosmicRun = param.get<bool>("CosmicRun", false);

  //Purity Config 
  mincount         = param.get<float>("MinCount", 100);
  minuniqcount     = param.get<float>("MinUniqueCount", 50);
  chi2cut          = param.get<float>("chi2cut", 10);
  pcacut           = param.get<float>("pcacut", 1.7);
  shapingtime      = param.get<float>("shapingtime", 2);
  anglecut         = param.get<float>("anglecut", 60);
  lowtaulimit      = param.get<double>("lowtaulimit", 10);
  hitaulimit       = param.get<double>("hitaulimit", 50000);
  lowsigmalimit    = param.get<double>("lowsigmalimit", 0);
  hisigmalimit     = param.get<double>("hisigmalimit", 6000);
  lowdqdxolimit    = param.get<double>("lowdqdxolimit", 100);
  hidqdxolimit     = param.get<double>("highdqdxolimit", 40000);
  FirstSig         = param.get<double>("FirstSig", 4.5);
  SecondSig        = param.get<double>("FirstSig", 4.5);
  Anglecut         = param.get<bool>("Anglecut", true);
  LifetimePlots    = param.get<bool>("LifetimePlots", false);
  fforceanglecut   = param.get<bool>("fforcedanglecut", false);


}

void Analysis::AnalyzeEvent(art::Event const & event) {

  _event_ind ++;

  // Getting the Hit Information                                                
  art::Handle<std::vector<recob::Hit> > hitListHandle;
  std::vector<art::Ptr<recob::Hit> > rawhits;
  if(event.getByLabel(_config.fHitsModuleLabel,hitListHandle))
    {art::fill_ptr_vector(rawhits, hitListHandle);}
  std::cout << "Number of Hits: " << rawhits.size() << std::endl;           

  //Purity Trigger - Gray you will probably want to change this for syntax
  double lifetime = -1;
  if(_config.fCosmicRun == true && _config.fDoPurityAna){
    lifetime = CalculateLifetime(rawhits, _config);
    lifetime = lifetime/2; //for microsecond
  } 

 
  // else{
//     if (_config.n_headers > 0 && _config.fUseNevisClock && _config.fDoPurityAna) {
//       auto const &headers_handle = event.getValidHandle<std::vector<daqAnalysis::HeaderData>>(_config.daq_tag);
//       //Take the fisrt FEM header. 
//       auto const header = headers_handle->at(0);
      
//       //The first event has the subrun is triggered by the $30. We need to store that.   
//       if((header.sub_run_no) != _sub_run_holder){
//         _sub_run_holder = header.sub_run_no;
//         // _sub_run_start_time = (header.frame_number)*_config.frame_to_dt + (header.two_mhzsample)*5e-7;
//       }

//       //Do The purity calculation if its within the limit of the clock 
//       //std::cout << "timestamp: " << ((header.frame_number)*_config.frame_to_dt + (header.two_mhzsample)*5e-7) - _prev_start_time << std::endl;
//         if((((header.frame_number)*_config.frame_to_dt + (header.two_mhzsample)*5e-7) - _sub_run_start_time ) > 6.5 + 0.2){ 
// 	lifetime = CalculateLifetime(rawhits, false);
// 	std::cout<<"Lifetime = "<<lifetime<<" ticks\n";
//       }
//       //The $30 clock runs at 8ns a tick, the cosmics start at 6.5 seconds in. The Nevis clock is 64MHz. Hence for every tick of the $30~1/2 a tick in the Nevis clock. You feel really useful when all you have done is to put this line in. We might want to hard code the in the config the buffer times. config.frame_to_dt needs to be checked. Also two_mhzsample is 2 in the test data.  

//     }
//     else if(_config.n_headers > 0  && _config.fDoPurityAna) {
//       //Get the Unix time stamp given to the fragment.
//       std::uint64_t timestamp = (event.time()).value();
      
//       //Check to see if the subrun has changed                                           
//       if((event.subRun()) != _sub_run_holder){
// 	_sub_run_holder = event.subRun();
// 	_sub_run_start_time = timestamp;
//       }

//       if(timestamp  - _sub_run_start_time > 6.7){
// 	lifetime = CalculateLifetime(rawhits, false);
// 	std::cout<<"Lifetime = "<<lifetime<<" ticks\n";
//       }//ADD PURITY FUNCTION HERE 
//     }
//     // or metadata if that's how we're doing things 
//     else if (_config.n_metadata > 0  && _config.fUseNevisClock && _config.fDoPurityAna) {
//       auto const &metadata_handle = event.getValidHandle<std::vector<daqAnalysis::NevisTPCMetaData>>(_config.daq_tag);

//       //Take only the first FEM header
//       auto const metadata = metadata_handle->at(0);



//       //The first event has the subrun is triggered by the $30. We need to store 
//       if((metadata.sub_run_no) != _sub_run_holder){
//         _sub_run_holder = metadata.sub_run_no;
//         _sub_run_start_time = (metadata.frame_number)*_config.frame_to_dt + (metadata.two_mhzsample)*5e-7;
//       }

// 	//Do The purity calculation
//       if((((metadata.frame_number)*_config.frame_to_dt + (metadata.two_mhzsample)*5e-7) -  _sub_run_start_time) > 6.7){ 	  
// 	lifetime = CalculateLifetime(rawhits, false);
// 	  std::cout<<"Lifetime = "<<lifetime<<" ticks\n";
// 	}
//     }
// else if(_config.n_metadata > 0  && _config.fDoPurityAna) {
//       //Get the Unix time stamp given to the fragment.
//       std::uint64_t timestamp = (event.time()).value();
      
//       //Check to see if the subrun has changed                                           
//       if((event.subRun()) != _sub_run_holder){
// 	_sub_run_holder = event.subRun();
// 	_sub_run_start_time = timestamp;
//       }
      
//       //If if there is 0.03 seconds between the events. If so maybe we have gone to COSMICON
//       if(timestamp  - _sub_run_start_time > 6.7){
// 	lifetime = CalculateLifetime(rawhits, false);
// 	std::cout<<"Lifetime = "<<lifetime<<" ticks\n";
//       }//ADD PURITY FUNCTION HERE 
      
//     }
  
//     else if(_config.fDoPurityAna){
//       //Get the Unix time stamp given to the fragment.
//       std::uint64_t timestamp = (event.time()).value();
      
//       //Check to see if the subrun has changed 
//       if((event.subRun()) != _sub_run_holder){
//         _sub_run_holder = event.subRun();
//         _sub_run_start_time = timestamp;
//       }

//       if(timestamp  - _sub_run_start_time > 6.7){
// 	lifetime = CalculateLifetime(rawhits, false);
// 	std::cout<<"Lifetime = "<<lifetime<<" ticks\n";
//       }//ADD PURITY FUNCTION HERE 
//     }
//  }

  //Finally add the Lifetime to the event info information 
  if(_config.fDoPurityAna){
      ProcessEventInfo(lifetime);
  }

  // clear out containers from last iter
  for (unsigned i = 0; i < _channel_map->NChannels(); i++) {
    _per_channel_data[i].waveform.clear();
    _per_channel_data[i].fft_real.clear();
    _per_channel_data[i].fft_imag.clear();
    _per_channel_data[i].peaks.clear();
  }
  // also for summed waveforms
  if (_config.sum_waveforms) {
    for (unsigned i = 0; i < _channel_map->NFEM(); i++) {
      _fem_summed_waveforms[i].clear();
      if (_config.fft_summed_waveforms) {
        _fem_summed_fft[i].clear();
      }
    }
  }
  _analyzed = true;

  //Get the raw_digits 
  art::Handle<std::vector<raw::RawDigit> > raw_digits_handle;
  std::vector<art::Ptr<raw::RawDigit> > rawdigits;
  if(event.getByLabel(_config.daq_tag,raw_digits_handle))
    {art::fill_ptr_vector(rawdigits, raw_digits_handle);}
  //  std::cout << "raw_digits_handle->size(): " << raw_digits_handle->size() << std::endl;

  //if we don't have the RawHits we can't use that method.
  if(!event.getByLabel(_config.fHitsModuleLabel,hitListHandle) && (_config.fUseRawHits || _config.fProcessRawHits)) {
    std::cerr << "ERROR: No Raw Hits Present\n" << std::endl;
  }

  unsigned index = 0;
  // calculate per channel stuff 
  if (_config.fUseRawHits || _config.fProcessRawHits) {
    // === Associations between hits and raw digits === 
    art::FindManyP<recob::Hit>   fmhr(raw_digits_handle, event,_config.fHitsModuleLabel);

    // loop over all tracks in the handle and get their hits
    for(size_t digit_int = 0; digit_int < raw_digits_handle->size(); ++digit_int){
      //Get the raw digit object.    
      auto const& digits=rawdigits[digit_int];  
      //Get the hits associated to the raw digit. 
      const std::vector<art::Ptr<recob::Hit> >& hits = fmhr.at(digit_int);
      _channel_index_map[digits->Channel()] = index;
      ProcessChannel(*digits, hits);
      index++;
    }
  }
  else {
    for (auto const& digits: *raw_digits_handle) {
      _channel_index_map[digits.Channel()] = index;
      ProcessChannel(digits);
      index++;
    }
  }

  if (_config.timing) {
    _timing.StartTime();
  }
  // make the reduced channel data stuff if need be
  if (_config.reduce_data) {
    for (size_t i = 0; i < _per_channel_data.size(); i++) {
      _per_channel_data_reduced[i] = daqAnalysis::ReducedChannelData(_per_channel_data[i]);
    }
  }
  if (_config.timing) {
    _timing.EndTime(&_timing.reduce_data);
  }

  if (_config.timing) {
    _timing.StartTime();
  }

  // now calculate stuff that depends on stuff between channels

  // DNoise
  for (unsigned i = 0; i < _channel_map->NChannels() - 1; i++) {
    unsigned next_channel = i + 1; 

    if (!_per_channel_data[i].empty && !_per_channel_data[next_channel].empty) {
      unsigned raw_digits_i = _channel_index_map[i];
      unsigned raw_digits_next_channel = _channel_index_map[next_channel];
      float unscaled_dnoise = _noise_samples[i].DNoise(
          (*raw_digits_handle)[raw_digits_i].ADCs(), _noise_samples[next_channel], (*raw_digits_handle)[raw_digits_next_channel].ADCs());
      // Don't use same noise sample to scale dnoise
      // This should probably be ok, as long as the dnoise sample is large enough

      // but special case when rms is too small
      if (_per_channel_data[i].rms > 1e-4 && _per_channel_data[next_channel].rms > 1e-4) {
        float dnoise_scale = sqrt(_per_channel_data[i].rms * _per_channel_data[i].rms + 
                                  _per_channel_data[next_channel].rms * _per_channel_data[next_channel].rms);
    
        _per_channel_data[i].next_channel_dnoise = unscaled_dnoise / dnoise_scale; 
      }
      else {
        _per_channel_data[i].next_channel_dnoise = 1.;
      }
    }
  }
  // don't set last dnoise
  _per_channel_data[_channel_map->NChannels() - 1].next_channel_dnoise = 0;

  if (_config.timing) {
    _timing.EndTime(&_timing.coherent_noise_calc);
  }

  if (_config.timing) {
    _timing.StartTime();
  }
  // deal with the header
  if (_config.n_headers > 0) {
    auto const &headers_handle = event.getValidHandle<std::vector<daqAnalysis::HeaderData>>(_config.daq_tag);
    for (auto const &header: *headers_handle) {
      ProcessHeader(header);
    }
  }
  // or metadata if that's how we're doing things
  if (_config.n_metadata > 0) {
    auto const &metdata_handle = event.getValidHandle<std::vector<daqAnalysis::NevisTPCMetaData>>(_config.daq_tag);
    for (auto const &metadata: *metdata_handle) {
      ProcessMetaData(metadata);
    }
  }
  if (_config.timing) {
    _timing.EndTime(&_timing.copy_headers);
  }
  // print stuff out
  if (_config.verbose) {
    std::cout << "EVENT NUMBER: " << _event_ind << std::endl;
    for (auto &channel_data: _per_channel_data) {
      std::cout << channel_data.Print();
    }
  }
  if (_config.timing) {
    _timing.Print();
  }
}

void Analysis::SumWaveforms(art::Event const & event) {
  auto const& raw_digits_handle = event.getValidHandle<std::vector<raw::RawDigit>>(_config.daq_tag);
  // summed waveforms
  if (_config.sum_waveforms) {
    size_t n_fem = _channel_map->NFEM();
    std::vector<std::vector<const std::vector<int16_t> *>> channel_waveforms_per_fem(n_fem);
    std::vector<std::vector<int16_t>> all_baselines(n_fem);
    // collect the waveforms
    for (unsigned i = 0; i < _channel_map->NChannels(); i++) {
      unsigned raw_digits_i = _channel_index_map[i];
      unsigned channel_ind = _channel_map->Wire2Channel(i);
      daqAnalysis::ReadoutChannel info = _channel_map->Ind2ReadoutChannel(channel_ind); 
      unsigned fem_ind = _channel_map->SlotIndex(info);
      channel_waveforms_per_fem[fem_ind].push_back(&(*raw_digits_handle)[raw_digits_i].ADCs());
      all_baselines[fem_ind].push_back(_per_channel_data[i].baseline);
    }
    // sum all of them
    for (unsigned i = 0; i < n_fem; i++) {
      daqAnalysis::SumWaveforms(_fem_summed_waveforms[i] ,channel_waveforms_per_fem[i], all_baselines[i]);
      // do fft if configed
      if (_config.fft_summed_waveforms) {
        if (_fft_manager.InputSize() != _fem_summed_waveforms[i].size()) {
          _fft_manager.Set(_fem_summed_waveforms[i].size());
        }
        // fill up fft array
        for (unsigned adc_ind = 0; adc_ind < _fem_summed_waveforms[i].size(); adc_ind++) {
          double *input = _fft_manager.InputAt(adc_ind);
          *input = (double) _fem_summed_waveforms[i][adc_ind];
        }
        // run the FFT
        _fft_manager.Execute();
        // return results
        unsigned adc_fft_size = _fft_manager.OutputSize();
        for (unsigned adc_ind = 0; adc_ind < adc_fft_size; adc_ind++) {
          _fem_summed_fft[i].push_back(_fft_manager.AbsOutputAt(adc_ind));
        }
      }
    }
  }
}

void Analysis::ProcessHeader(const daqAnalysis::HeaderData &header) {
  _header_data[_channel_map->SlotIndex(header)] = header;
}
	      
void Analysis::ProcessEventInfo(double &lifetime){
    //Sorry for wasting space and making a vector. 
    _event_info.SetPurity(lifetime); 
}

void Analysis::ProcessMetaData(const daqAnalysis::NevisTPCMetaData &metadata) {
  _nevis_tpc_metadata[_channel_map->SlotIndex(metadata)] = metadata;
}

void Analysis::ProcessChannel(const raw::RawDigit &digits, const std::vector<art::Ptr<recob::Hit> > &hits){

  auto channel = digits.Channel();

  // uses the RawHitFinder Module to find the peak and then processes the channel as before.
  if (_config.fUseRawHits) {
    PeakFinder peaks(hits);
    _per_channel_data[channel].peaks.assign(peaks.Peaks()->begin(), peaks.Peaks()->end());
  }

  // fill up channel data even if we're using PeakFinder
  _per_channel_data[channel].Hitoccupancy = hits.size();
  _per_channel_data[channel].Hitmean_peak_height = _per_channel_data[channel].meanPeakHeight(hits);


  ProcessChannel(digits);
}


void Analysis::ProcessChannel(const raw::RawDigit &digits) {
  auto channel = digits.Channel();
  if (channel >= _channel_map->NChannels()) return;
  // handle empty events
  if (digits.NADC() == 0) {
    // default constructor handles empty event
    _per_channel_data[channel] = ChannelData(channel);
    _noise_samples[channel] = NoiseSample();
    return;
  }
   
  // if there are ADC's, the channel isn't empty
  _per_channel_data[channel].empty = false;
 
  // re-allocate FFT if necessary
  if (_fft_manager.InputSize() != digits.NADC()) {
    _fft_manager.Set(digits.NADC());
  }
   
  _per_channel_data[channel].channel_no = channel;

  int16_t max = -INT16_MAX;
  int16_t min = INT16_MAX;
  auto adc_vec = digits.ADCs();
  if (_config.timing) {
    _timing.StartTime();
  }
  auto n_adc = digits.NADC();
  if (_config.fill_waveforms || _config.fft_per_channel) {
    for (unsigned i = 0; i < n_adc; i ++) {
      int16_t adc = adc_vec[i];
    
      // fill up waveform
       if (_config.fill_waveforms) {
        if (adc > max) max = adc;
        if (adc < min) min = adc;

        _per_channel_data[channel].waveform.push_back(adc);
      }

      if (_config.fft_per_channel) {
        // fill up fftw array
        double *input = _fft_manager.InputAt(i);
        *input = (double) adc;
      }
    }
  }

  if (_config.timing) {
    _timing.EndTime(&_timing.fill_waveform);
  }
  if (_config.timing) {
    _timing.StartTime();
  }
  if (_config.baseline_calc == 0) {
    _per_channel_data[channel].baseline = 0;
  }
  else if (_config.baseline_calc == 1) {
    _per_channel_data[channel].baseline = digits.GetPedestal();
  }
  else if (_config.baseline_calc == 2) {
    _per_channel_data[channel].baseline = Mode(digits.ADCs(), _config.n_mode_skip);
  }
  if (_config.timing) {
    _timing.EndTime(&_timing.baseline_calc);
  }

  _per_channel_data[channel].max = max;
  _per_channel_data[channel].min = min;
  
  if (_config.timing) {
    _timing.StartTime();
  }
  // calculate FFTs
  if (_config.fft_per_channel) {
    _fft_manager.Execute();
    int adc_fft_size = _fft_manager.OutputSize();
    for (int i = 0; i < adc_fft_size; i++) {
      _per_channel_data[channel].fft_real.push_back(_fft_manager.ReOutputAt(i));
      _per_channel_data[channel].fft_imag.push_back(_fft_manager.ImOutputAt(i));
    } 
  }
  if (_config.timing) {
    _timing.EndTime(&_timing.execute_fft);
  }

  // Run Peak Finding only if we aren't depending on RawHitFinder for that part
  if(!_config.fUseRawHits){
    if (_config.timing) {
      _timing.StartTime();
    }
    // get thresholds 
    float threshold = _config.threshold;
    if (_config.threshold_calc == 0) {
      threshold = _config.threshold;
    }
    else if (_config.threshold_calc == 1) {
      auto thresholds = Threshold(adc_vec, _per_channel_data[channel].baseline, _config.threshold_sigma, _config.verbose);
      threshold = thresholds.Val();
    }
    else if (_config.threshold_calc == 2) {
      NoiseSample temp({{0, (unsigned)digits.NADC()-1}}, _per_channel_data[channel].baseline);
      float raw_rms = temp.RMS(adc_vec);
      threshold = raw_rms * _config.threshold_sigma;
    }
    else if (_config.threshold_calc == 3) {
      // if using plane data, make collection planes reach a higher threshold
      float n_sigma = _config.threshold_sigma;
      if (_config.use_planes && _channel_map->PlaneType(channel) == 2) n_sigma = n_sigma * 1.5;
    
      threshold = _thresholds[channel].Threshold(adc_vec, _per_channel_data[channel].baseline, n_sigma);
    }
    if (_config.timing) {
      _timing.EndTime(&_timing.calc_threshold);
    }

    _per_channel_data[channel].threshold = threshold;

    if (_config.timing) {
      _timing.StartTime();
    }
    // get Peaks
    unsigned peak_plane = (_config.use_planes) ? _channel_map->PlaneType(channel) : 0;
  
    PeakFinder peaks(adc_vec, _per_channel_data[channel].baseline, threshold, 
        _config.n_smoothing_samples, _config.n_above_threshold, peak_plane);
    _per_channel_data[channel].peaks.assign(peaks.Peaks()->begin(), peaks.Peaks()->end());
    if (_config.timing) {
      _timing.EndTime(&_timing.find_peaks);
    }
  }

  if (_config.timing) {
    _timing.StartTime();
  }
  // get noise samples
  if (_config.noise_range_sampling == 0) {
    // use first n_noise_samples
    _noise_samples[channel] = NoiseSample( { { 0, _config.n_noise_samples -1 } }, _per_channel_data[channel].baseline);
  }
  else {
    // or use peak finding
    _noise_samples[channel] = NoiseSample(_per_channel_data[channel].peaks, _per_channel_data[channel].baseline, digits.NADC()); 
  }

  // Refine baseline values by taking the mean over the background range
  if (_config.refine_baseline) {
    _noise_samples[channel].ResetBaseline(adc_vec);
    _per_channel_data[channel].baseline = _noise_samples[channel].Baseline(); 
  }

  _per_channel_data[channel].rms = _noise_samples[channel].RMS(adc_vec);
  _per_channel_data[channel].noise_ranges = *_noise_samples[channel].Ranges();
  if (_config.timing) {
    _timing.EndTime(&_timing.calc_noise);
  }

  // register rms if using running threshold
  if (_config.threshold_calc == 3 && !_config.fUseRawHits) {
    _thresholds[channel].AddRMS(_per_channel_data[channel].rms);
  }

  // calculate derived quantities
  _per_channel_data[channel].occupancy = _per_channel_data[channel].Occupancy();
  _per_channel_data[channel].mean_peak_height = _per_channel_data[channel].meanPeakHeight();
}

bool Analysis::ReadyToProcess() {
  return _analyzed;
}

bool Analysis::EmptyEvent() {
  return _per_channel_data[0].empty;
}

void Timing::StartTime() {
  start = std::chrono::high_resolution_clock::now(); 
}
void Timing::EndTime(float *field) {
  auto now = std::chrono::high_resolution_clock::now();
  *field += std::chrono::duration<float, std::milli>(now- start).count();
}
void Timing::Print() {
  std::cout << "FILL WAVEFORM: " << fill_waveform << std::endl;
  std::cout << "CALC BASELINE: " << baseline_calc << std::endl;
  std::cout << "FFT   EXECUTE: " << execute_fft << std::endl;
  std::cout << "CALC THRESHOLD " << calc_threshold << std::endl;
  std::cout << "CALC PEAKS   : " << find_peaks << std::endl;
  std::cout << "CALC NOISE   : " << calc_noise << std::endl;
  std::cout << "REDUCE DATA  : " << reduce_data << std::endl;
  std::cout << "COHERENT NOISE " << coherent_noise_calc << std::endl;
  std::cout << "COPY HEADERS : " << copy_headers << std::endl;
}

