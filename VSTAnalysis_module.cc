#include <vector>

#include "TROOT.h"
#include "TTree.h"

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "canvas/Utilities/InputTag.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h" 
#include "art/Framework/Principal/SubRun.h" 
#include "art/Framework/Services/Optional/TFileService.h"

#include "ChannelData.hh"
#include "HeaderData.hh"
#include "Analysis.hh"

/*
 * Uses the Analysis class to print stuff to file
*/

namespace daqAnalysis {
  class VSTAnalysis;
}


class daqAnalysis::VSTAnalysis : public art::EDAnalyzer {
public:
  explicit VSTAnalysis(fhicl::ParameterSet const & p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  VSTAnalysis(VSTAnalysis const &) = delete;
  VSTAnalysis(VSTAnalysis &&) = delete;
  VSTAnalysis & operator = (VSTAnalysis const &) = delete;
  VSTAnalysis & operator = (VSTAnalysis &&) = delete;

  // Required functions.
  void analyze(art::Event const & e) override;
private:
  daqAnalysis::Analysis _analysis;
  TTree *_output;
};

daqAnalysis::VSTAnalysis::VSTAnalysis(fhicl::ParameterSet const & p):
  art::EDAnalyzer::EDAnalyzer(p),
  _analysis(p)
{
  art::ServiceHandle<art::TFileService> fs;
  _output = fs->make<TTree>("event", "event");
  // which data to use
  if (_analysis._config.reduce_data) {
    _output->Branch("channel_data", &_analysis._per_channel_data_reduced);
  }
  else {
    _output->Branch("channel_data", &_analysis._per_channel_data);
  }
  if (_analysis._config.sum_waveforms) {
    _output->Branch("summed_waveforms", &_analysis._fem_summed_waveforms);
  }
}

void daqAnalysis::VSTAnalysis::analyze(art::Event const & e) {
    _analysis.AnalyzeEvent(e);
    // call sum waveforms explicitly
  _analysis.SumWaveforms(e);
  if (_analysis.ReadyToProcess()) {
     _output->Fill();
  }
}


DEFINE_ART_MODULE(daqAnalysis::VSTAnalysis)
