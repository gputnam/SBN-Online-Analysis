#ifndef VSTPURITY_H_SEEN
#define VSTPURITY_H_SEEN


///////////////////////////////////////////////
// Purity.h
//
// Functions for VST online purity analysis
///////////////////////////////////////////////

// framework
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h" 
#include "art/Framework/Principal/Handle.h" 
#include "canvas/Persistency/Common/Ptr.h" 
#include "canvas/Persistency/Common/PtrVector.h" 
#include "art/Framework/Services/Registry/ServiceHandle.h" 
#include "art/Framework/Services/Optional/TFileService.h" 
#include "art/Framework/Services/Optional/TFileDirectory.h" 
#include "messagefacility/MessageLogger/MessageLogger.h" 
#include "canvas/Persistency/Common/FindManyP.h"

// LArSoft 
#include "lardataobj/RecoBase/Hit.h"
#include "larcore/Geometry/Geometry.h"
#include "larcorealg/Geometry/GeometryCore.h"

// c++
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>

// ROOT
#include "TTree.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "TF1.h"
#include "TStyle.h"
#include "TAttMarker.h"	
#include "TAxis.h"
#include "TMath.h"
#include "TH2.h"
#include "Math/Functor.h"
#include "Math/Minimizer.h"
#include "Math/Factory.h"
#include "TPrincipal.h"

//Online Mointering Includes 
#include "Analysis.hh"

namespace daqAnalysis{ 
  
  //  struct Analysis::AnalysisConfig;
  double CalculateLifetime(std::vector<art::Ptr<recob::Hit>> rawhits,const struct Analysis::AnalysisConfig& _config); //Calculate electron lifetime from collection of hits
}

#endif
