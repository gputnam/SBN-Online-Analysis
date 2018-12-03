#ifndef _sbnddaq_analysis_EventInfo
#define _sbnddaq_analysis_EventInfo

namespace daqAnalysis {
  // Information calculated rom the Event 
  class EventInfo {
  public:

    double purity;

    EventInfo():
      purity(0)
    {}

    void SetPurity(double lifetime) {purity = lifetime;}

  };
}

#endif
