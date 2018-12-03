#include <string>
#include <vector>
#include <stdint.h>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
}

#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "canvas/Persistency/Common/Ptr.h"
#include "canvas/Persistency/Common/PtrVector.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "cetlib/exception.h"
#include "cetlib/search_path.h"

#include "larcore/Geometry/Geometry.h"

#include "lardataobj/RawData/RawDigit.h"
#include "lardataobj/RawData/raw.h"
#include "lardataobj/RecoBase/Wire.h"
#include "lardata/Utilities/LArFFT.h"
#include "lardataobj/Utilities/sparse_vector.h"
#include "lardata/Utilities/AssociationUtil.h"

/*
  * "Calibration" module for the Vertical Slice Test.
  * Does no calibration. This is just here so we can use
  * RawHitFinder et al. in online monitoring
*/

namespace caldata {

  class CalWireVST : public art::EDProducer {
  public:
    typedef lar::sparse_vector<float> RegionsOfInterest_t;

    explicit CalWireVST(fhicl::ParameterSet const& pset);

    void produce(art::Event& evt);
    void beginJob();
    void endJob();
    void reconfigure(fhicl::ParameterSet const& p);

  protected:
    std::string digit_module_label;
  };

  DEFINE_ART_MODULE(CalWireVST)


  CalWireVST::CalWireVST(fhicl::ParameterSet const& pset) {
    this->reconfigure(pset);
    produces< std::vector<recob::Wire> >();
    produces<art::Assns<raw::RawDigit, recob::Wire>>();

  }

  void CalWireVST::reconfigure(fhicl::ParameterSet const& param) {
    digit_module_label = param.get<std::string>("digit_producer_name");

  }
  void CalWireVST::beginJob() {}
  void CalWireVST::endJob() {}

  void CalWireVST::produce(art::Event &evt) {
    // get the geometry
    art::ServiceHandle<geo::Geometry> geom;

    // make a collection of Wires
    std::unique_ptr<std::vector<recob::Wire> > wirecol(new std::vector<recob::Wire>);

    // ... and an association set     --Hec
    std::unique_ptr<art::Assns<raw::RawDigit,recob::Wire> > WireDigitAssn
      (new art::Assns<raw::RawDigit,recob::Wire>);

    // get the raw digits
    art::Handle< std::vector<raw::RawDigit> > digitVecHandle;
    evt.getByLabel(digit_module_label, digitVecHandle);

    unsigned short n_samples = art::Ptr<raw::RawDigit>(digitVecHandle, 0)->Samples();
    // holder for "processed" raw digits
    std::vector<float> this_raw_digits(n_samples);
    // holder for uncompressed raw digits
    std::vector<short> rawadc(n_samples);
    for (size_t i = 0; i < digitVecHandle->size(); i++) {
      // clear out containers
      this_raw_digits.clear();
      rawadc.clear();

      art::Ptr<raw::RawDigit> digitVec(digitVecHandle, i);
      float pedestal = digitVec->GetPedestal();
      int channel = digitVec->Channel();

      // uncompress the data
      raw::Uncompress(digitVec->ADCs(), rawadc, digitVec->Compression());

      // do pedestal subtraction
      for (size_t j = 0; j < digitVec->Samples(); j++) {
        this_raw_digits.push_back( rawadc[j] - pedestal);
      }
      // set the "Regions of interest" to the whole waveform
      RegionsOfInterest_t roi(this_raw_digits);
      //roi.add_range(0,this_raw_digits.begin(),this_raw_digits.end());
      // get the geometry
      auto view = geom->View(digitVec->Channel());
      // add "Wire" object
      wirecol->emplace_back(roi, channel, view);
      // associate the new wire object with the raw-digits
      util::CreateAssn(*this, evt, *wirecol, digitVec, *WireDigitAssn);
    }

    // put stuff in the event
    evt.put(std::move(wirecol));        //--Hec
    evt.put(std::move(WireDigitAssn));  //--Hec

  }
} // namespace caldata







