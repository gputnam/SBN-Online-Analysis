#ifndef RedisData_h
#define RedisData_h

#include <vector>
#include <ctime>
#include <cmath>
#include <iostream>
#include <stdlib.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#include "messagefacility/MessageLogger/MessageLogger.h"

#include "../ChannelData.hh"
#include "../HeaderData.hh"
#include "../VSTChannelMap.hh"
#include "../EventInfo.hh"

namespace daqAnalysis {
  // stream types
  class StreamDataMean;
  class StreamDataVariableMean;
  class StreamDataMax;
  class StreamDataSum;
  class StreamDataRMS;

  // detector metric base class
  template<class Stream, char const *REDIS_NAME>
  class DetectorMetric;

  // and the metric classes
  class RedisRMS;
  class RedisOccupancy;
  class RedisBaseline;
  class RedisBaselineRMS;
  class RedisPulseHeight;
  class RedisDNoise;
  class RedisRawHitOccupancy;
  class RedisRawHitPulseHeight;
  class RedisPurity;

  // header metric base class
  template<class Stream, char const *REDIS_NAME>
  class HeaderMetric;

    // and the metric classes
  class RedisEventNo;
  class RedisFrameNo;
  class RedisTrigFrameNo;
  class RedisBlocks;

  ///Event (channel independent metrics ) 
  template<class Stream, char const *REDIS_NAME> 
  class EventInfoMetric;

  // and the metric classes 
  class RedisPurity;

}

// keeps a running mean of a metric w/ n_data instances and n_points_per_time data points per each time instance
class daqAnalysis::StreamDataMean {
public:
  StreamDataMean(unsigned n_data, unsigned n_points_per_time): _data(n_data, 0.), _n_values(0), _instance_data(n_data, 0.), _n_points_per_time(n_data, n_points_per_time) {}

  // add in a new value
  void Fill(unsigned instance_index, unsigned datum_index, float datum);
  // clear values
  void Clear();
  // take the data value and reset it
  float Data(unsigned index);

  // returns n_data
  unsigned Size() { return _data.size(); }
  // called per iter
  void Update();
  // update a points per time value
  void SetPointsPerTime(unsigned index, unsigned points)
    { _n_points_per_time[index] = points; }

protected:
  // add data from this time instance into the main data container
  void AddInstance(unsigned index);

  // internal data
  std::vector<float> _data;
  // number of values averaged together in each data point
  unsigned _n_values;

  // average of values for this time instance
  std::vector<float> _instance_data;
  // number of values averaged together in each time instance
  // can be different per data point
  std::vector<unsigned> _n_points_per_time;
};

// keeps a running mean of a metric w/ n_data data points
// where each data point may have a different number of entries and may
// receive a different number of values at each time instance 
class daqAnalysis::StreamDataVariableMean {
public:
  // n_points_per_time is not used in this class
  StreamDataVariableMean(unsigned n_data, unsigned _): _data(n_data, 0.), _n_values(n_data, 0), _instance_data(n_data, 0), _n_values_current_instance(n_data,0) {}

  // add in a new value
  void Fill(unsigned instance_index, unsigned datum_index, float datum);
  // take the data value and reset it
  float Data(unsigned index);
  // returns n_data
  unsigned Size() { return _data.size(); }
  // called per iter
  void Update();
  // clear values
  void Clear();

  // update a points per time value
  // does nothing, since all points per time values are dynamic for this class
  void SetPointsPerTime(unsigned index, unsigned points) {}

protected:
  // add data from this time instance into the main data container
  void AddInstance(unsigned index);

  // internal data
  std::vector<float> _data;
  // number of values averaged together in each data point
  std::vector<unsigned> _n_values;

  // data of the current time instance
  std::vector<float> _instance_data;
  // number of values averaged together in the current time instance
  std::vector<unsigned> _n_values_current_instance;
};

// keeps running max value of a metric w/ n instances
class daqAnalysis::StreamDataMax {
public:
  StreamDataMax(unsigned n_data, unsigned _): _data(n_data, 0) {}

  void Fill(unsigned instance_index, unsigned datum_index, unsigned datum);
  unsigned Data(unsigned index);
  unsigned Size() { return _data.size(); }
  void Update() {/* doesn't need to do anything currently */}
  // clear values
  void Clear();

protected:
  std::vector<unsigned> _data;
};

// keeps running sum of a metric w/ n instances
class daqAnalysis::StreamDataSum {
public:
  StreamDataSum(unsigned n_data, unsigned _): _data(n_data, 0) {}

  void Fill(unsigned instance_index, unsigned datum_index, unsigned datum);
  unsigned Data(unsigned index);
  unsigned Size() { return _data.size(); }
  void Update() {/* doesn't need to do anything currently */}
  // clear values
  void Clear();

protected:
  std::vector<unsigned> _data;
};

// keeps track of running RMS value 
class daqAnalysis::StreamDataRMS {
public:
  StreamDataRMS(unsigned n_data, unsigned n_points_per_time): 
    _means(n_data, StreamDataMean(n_points_per_time, 1)), 
    _rms(n_data, std::vector<float>(n_points_per_time)), 
    _n_values(0) 
  {}

  // add in a new value
  void Fill(unsigned instance_index, unsigned datum_index, float datum);
  // clear values
  void Clear();
  // take the data value and reset it
  float Data(unsigned index);
  // returns n_data
  unsigned Size() { return _rms.size(); }
  // called per iter
  void Update();
  // update a points per time value
  void SetPointsPerTime(unsigned index, unsigned points) {
    _means[index] = StreamDataMean(points, 1);
    _rms[index].resize(points);
  }

protected:
  // internal data
  std::vector<daqAnalysis::StreamDataMean> _means;
  std::vector<std::vector<float>> _rms;
  unsigned _n_values;
};

// holds a StreamDataMean or StreamDataVariableMean across all instances of the detector
// i.e. per crate, fem, wire, etc.
template<class Stream, char const *REDIS_NAME>
class daqAnalysis::DetectorMetric {
public:
  // how to calculate the metric given a channel data reference
  virtual float Calculate(daqAnalysis::ChannelData &channel) = 0;

  // base class destructors should be virtual
  virtual ~DetectorMetric() {}

  // implementing templated functions in header (because cpp is bad)

  // constructor
  DetectorMetric(daqAnalysis::VSTChannelMap *channel_map) :
    _wire_data(channel_map->NChannels(), 1),
    _fem_data(channel_map->NFEM(), 1),
    _crate_data(channel_map->NCrates(), channel_map->NChannels()), /* NOTE: assume there is only one crate */
    _wire_message_times(channel_map->NChannels(), 0)
  {
    // set the number of channels per fem
    for (unsigned slot = 0; slot < channel_map->NFEM(); slot++) {
      _fem_data.SetPointsPerTime(slot, channel_map->NSlotWire(slot));
    }
  }

  // add in data
  void Fill(daqAnalysis::ChannelData &channel, unsigned crate, unsigned crate_channel_ind, unsigned fem_ind, unsigned fem_channel_ind, unsigned wire) {
    // calculate and add to each container
    float dat = Calculate(channel);

    // persist through nan's
    if (std::isnan(dat)) {
      // don't send error messages too often
      time_t now = std::time(nullptr);
      if (now - _wire_message_times[wire] > 10) {
        _wire_message_times[wire] = now;
        mf::LogError("NAN Metric") << "Metric " << REDIS_NAME << " is NAN on wire " << wire << std::endl;
      }
      return;
    }

    // each container is aware of how often it is filled per time instance
    _crate_data.Fill(crate, crate_channel_ind, dat);
    _fem_data.Fill(fem_ind, fem_channel_ind, dat);
    _wire_data.Fill(wire, 0, dat);
  }

  float DataCrate(unsigned index) {
    return _crate_data.Data(index);
  }

  float DataFEM(unsigned index) {
    return _fem_data.Data(index);
  }

  float DataWire(unsigned index) {
    return _wire_data.Data(index);
  }

  // to be called once per time instance
  void Update() {
    _crate_data.Update();
    _fem_data.Update();
    _wire_data.Update();
  }

  // called after stuff is sent to Redis
  void Clear() {
    _crate_data.Clear();
    _fem_data.Clear();
    _wire_data.Clear();
  }

  // send stuff to Redis
  unsigned Send(redisContext *context, uint64_t index, const char *stream_name, unsigned stream_expire) {
    // send all the wire stuff
    unsigned n_wires = _wire_data.Size();
    for (unsigned wire = 0; wire < n_wires; wire++) {
      redisAppendCommand(context, "SET stream/%s:%lu:%s:wire:%i %f",
        stream_name, index, REDIS_NAME,wire, DataWire(wire)); 

      if (stream_expire != 0) {
        redisAppendCommand(context, "EXPIRE stream/%s:%lu:%s:wire:%i %u",
          stream_name, index, REDIS_NAME,wire, stream_expire); 
      }
    } 
    // and the fem stuff
    unsigned n_fem = _fem_data.Size();
    for (unsigned fem_ind = 0; fem_ind < n_fem; fem_ind++) {
      // @VST: this is ok because there is only 1 crate
      // TEMPORARY IMPLEMENTATION
      unsigned fem = fem_ind;
      unsigned crate = 0;
      redisAppendCommand(context, "SET stream/%s:%lu:%s:crate:%lu:fem:%i %f",
        stream_name, index, REDIS_NAME, crate, fem, DataFEM(fem_ind)); 

      if (stream_expire != 0) {
        redisAppendCommand(context, "EXPIRE stream/%s:%lu:%s:crate:%lu:fem:%i %u",
         stream_name, index, REDIS_NAME, crate, fem, stream_expire); 
      }
    } 
    // and the crate stuff
    unsigned n_crate = _crate_data.Size();
    for (unsigned crate = 0; crate < n_crate; crate++) {
      redisAppendCommand(context, "SET stream/%s:%lu:%s:crate:%i %f",
         stream_name, index, REDIS_NAME, crate, DataCrate(crate));

      if (stream_expire != 0) {
        redisAppendCommand(context, "EXPIRE stream/%s:%lu:%s:crate:%i %u",
           stream_name, index, REDIS_NAME, crate, stream_expire);
      }
    }
    // return number of commands sent
    return (n_wires + n_fem + n_crate) * ((stream_expire == 0) ? 1:2);
  }

  void Print(const char *stream_name) {
    std::cout << "METRIC: " << REDIS_NAME << std::endl;
    std::cout << "STREAM NAME: " << stream_name << std::endl;
    unsigned n_wires = _wire_data.Size();
    for (unsigned wire = 0; wire < n_wires; wire++) {
      std::cout << "WIRE: " << wire << " DATA: " << DataWire(wire) << std::endl;
    }
  }

protected:
  Stream _wire_data;
  Stream _fem_data;
  Stream _crate_data;

  std::vector<unsigned> _wire_message_times;
};

// string literals can't be template arguments for some reason, so declare them here

// also they can't be defined here becasue of stupid linking things
// see RedisData.cc for definitions
extern char REDIS_NAME_RMS[];
extern char REDIS_NAME_OCCUPANCY[];
extern char REDIS_NAME_DNOISE[];
extern char REDIS_NAME_BASLINE[];
extern char REDIS_NAME_BASELINE_RMS[];
extern char REDIS_NAME_PULSE_HEIGHT[];
extern char REDIS_NAME_RAWHIT_OCCUPANCY[];
extern char REDIS_NAME_RAWHIT_PULSE_HEIGHT[];

// RMS is Variable Mean because sometimes noise calculation algorithm can fail
class daqAnalysis::RedisRMS: public daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_RMS> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_RMS>::DetectorMetric;

  // implement calculate
 inline float Calculate(daqAnalysis::ChannelData &channel) override
   { return channel.rms; }
};


class daqAnalysis::RedisOccupancy: public daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_OCCUPANCY> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_OCCUPANCY>::DetectorMetric;

  // implement calculate
 inline float Calculate(daqAnalysis::ChannelData &channel) override
   { return channel.occupancy; }
};

class daqAnalysis::RedisRawHitOccupancy: public daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_RAWHIT_OCCUPANCY> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_RAWHIT_OCCUPANCY>::DetectorMetric;

  // implement calculate 
  inline float Calculate(daqAnalysis::ChannelData &channel) override
  { return channel.Hitoccupancy; }
};

class daqAnalysis::RedisDNoise: public daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_DNOISE> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_DNOISE>::DetectorMetric;

  // implement calculate
 inline float Calculate(daqAnalysis::ChannelData &channel) override
   { return channel.next_channel_dnoise; }
};

class daqAnalysis::RedisBaseline: public daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_BASLINE> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataMean, REDIS_NAME_BASLINE>::DetectorMetric;

  // implement calculate
 inline float Calculate(daqAnalysis::ChannelData &channel) override
   { return channel.baseline; }
};

class daqAnalysis::RedisBaselineRMS: public daqAnalysis::DetectorMetric<StreamDataRMS, REDIS_NAME_BASELINE_RMS> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataRMS, REDIS_NAME_BASELINE_RMS>::DetectorMetric;

  // implement calculate
 inline float Calculate(daqAnalysis::ChannelData &channel) override
   { return channel.baseline; }
};

class daqAnalysis::RedisPulseHeight: public daqAnalysis::DetectorMetric<StreamDataVariableMean, REDIS_NAME_PULSE_HEIGHT> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataVariableMean, REDIS_NAME_PULSE_HEIGHT>::DetectorMetric;

  // implement calculate
 inline float Calculate(daqAnalysis::ChannelData &channel) override
   { return channel.mean_peak_height; }
};

class daqAnalysis::RedisRawHitPulseHeight: public daqAnalysis::DetectorMetric<StreamDataVariableMean, REDIS_NAME_RAWHIT_PULSE_HEIGHT> {
  // inherit constructor
  using daqAnalysis::DetectorMetric<StreamDataVariableMean, REDIS_NAME_RAWHIT_PULSE_HEIGHT>::DetectorMetric;

  // implement calculate 
  inline float Calculate(daqAnalysis::ChannelData &channel) override
  {return channel.Hitmean_peak_height; }
};

template<class Stream, char const *REDIS_NAME>
class daqAnalysis::HeaderMetric {
public:
  // how to calculate the metric given a channel data reference
  virtual unsigned Calculate(daqAnalysis::HeaderData &header) = 0;

  // base class destructors should be virtual
  virtual ~HeaderMetric() {}

  HeaderMetric(daqAnalysis::VSTChannelMap *channel_map)
    : _fem(channel_map->NFEM(), 1)
  {}

  void Fill(daqAnalysis::HeaderData &header, unsigned fem_ind) {
    // calculate and add to each container
    unsigned val = Calculate(header);

    // each container is aware of how often it is filled per time instance
    _fem.Fill(fem_ind, 0, val);
  }

  unsigned Data(unsigned index) {
    return _fem.Data(index);
  }

  // to be called once per time instance
  void Update() {
    _fem.Update();
  }

  // called when stuff is sent to Redis
  void Clear() {
    _fem.Clear();
  }

  // send stuff to Redis
  unsigned Send(redisContext *context, uint64_t index, const char *stream_name, unsigned stream_expire) {
    // send FEM stuff
    unsigned n_fem = _fem.Size();
    for (unsigned fem_ind = 0; fem_ind < n_fem; fem_ind++) {
      // @VST: this is ok because there is only 1 crate
      // TEMPORARY IMPLEMENTATION
      unsigned fem = fem_ind;
      unsigned crate = 0;
      redisAppendCommand(context, "SET stream/%s:%lu:%s:crate:%lu:fem:%i %u",
        stream_name, index, REDIS_NAME, crate, fem, Data(fem_ind)); 

      if (stream_expire != 0) {
        redisAppendCommand(context, "EXPIRE stream/%s:%lu:%s:crate:%lu:fem:%i %u",
         stream_name, index, REDIS_NAME, crate, fem, stream_expire); 
      }
    } 
    return n_fem * ((stream_expire == 0) ? 1 : 2);
  }

protected:
  Stream _fem;

};

// string literals for header metric inheritor classes
extern char REDIS_NAME_EVENT_NO[];
extern char REDIS_NAME_FRAME_NO[];
extern char REDIS_NAME_TRIG_FRAME_NO[];
extern char REDIS_NAME_BLOCKS[];

class daqAnalysis::RedisEventNo: public daqAnalysis::HeaderMetric<StreamDataMax, REDIS_NAME_EVENT_NO> {
  // inherit constructor
  using daqAnalysis::HeaderMetric<StreamDataMax, REDIS_NAME_EVENT_NO>::HeaderMetric;

  // implement calculate
 inline unsigned Calculate(daqAnalysis::HeaderData &header) override
   { return header.event_number; }
};

class daqAnalysis::RedisFrameNo: public daqAnalysis::HeaderMetric<StreamDataMax, REDIS_NAME_FRAME_NO> {
  // inherit constructor
  using daqAnalysis::HeaderMetric<StreamDataMax, REDIS_NAME_FRAME_NO>::HeaderMetric;

  // implement calculate
 inline unsigned Calculate(daqAnalysis::HeaderData &header) override
   { return header.frame_number; }
};

class daqAnalysis::RedisTrigFrameNo: public daqAnalysis::HeaderMetric<StreamDataMax, REDIS_NAME_TRIG_FRAME_NO> {
  // inherit constructor
  using daqAnalysis::HeaderMetric<StreamDataMax, REDIS_NAME_TRIG_FRAME_NO>::HeaderMetric;

  // implement calculate
 inline unsigned Calculate(daqAnalysis::HeaderData &header) override
   { return header.trig_frame_number; }
};

class daqAnalysis::RedisBlocks: public daqAnalysis::HeaderMetric<StreamDataSum, REDIS_NAME_BLOCKS> {
  // inherit constructor
  using daqAnalysis::HeaderMetric<StreamDataSum, REDIS_NAME_BLOCKS>::HeaderMetric;

  // implement calculate
 inline unsigned Calculate(daqAnalysis::HeaderData &header) override
   { return 1; /* always 1 header per header */ }
};


template<class Stream, char const *REDIS_NAME>
class daqAnalysis::EventInfoMetric {
public:
  // how to calculate the metric given a channel data reference
  virtual float Calculate(daqAnalysis::EventInfo &event_info) = 0;

  // base class destructors should be virtual
  virtual ~EventInfoMetric() {}

  EventInfoMetric(daqAnalysis::VSTChannelMap *channel_map)
    : _event(1, 1)
  {}

  void Fill(daqAnalysis::EventInfo &event_info) {
    // calculate and add to each container
    float  val = Calculate(event_info);

    // each container is aware of how often it is filled per time instance Only one data stream hence first value is set to 0. 
    _event.Fill(0,0, val);
  }

  float Data() {
    //This stream has only one Data set per event. The purity does not change per FEM! (Well at least in lariat) 
    return _event.Data(0);
  }

  // to be called once per time instance
  void Update() {
    _event.Update();
  }

  // called when stuff is sent to Redis
  void Clear() {
    _event.Clear();
  }

  // send stuff to Redis
  unsigned Send(redisContext *context, unsigned index, const char *stream_name, unsigned stream_expire) {
    std::cout << "Data(): " << Data() << std::endl;
    redisAppendCommand(context, "SET stream/%s:%lu:%s: %f",
        stream_name, index, REDIS_NAME, Data()); 
      if (stream_expire != 0) {
        redisAppendCommand(context, "EXPIRE stream/%s:%lu:%s: %u",
         stream_name, index, REDIS_NAME, stream_expire); 
      } 
    return ((stream_expire == 0) ? 1 : 2);
  }

protected:
  Stream _event;

};

extern char REDIS_NAME_PURITY[];

class daqAnalysis::RedisPurity: public daqAnalysis::EventInfoMetric<StreamDataMean, REDIS_NAME_PURITY> {
  // inherit constructor
  using daqAnalysis::EventInfoMetric<StreamDataMean, REDIS_NAME_PURITY>::EventInfoMetric;

  // implement calculate
  inline  float Calculate(daqAnalysis::EventInfo &eventinfo) override
  {
    return eventinfo.purity;
  }
};


#endif
