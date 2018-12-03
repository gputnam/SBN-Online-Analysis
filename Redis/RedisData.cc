#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <cassert> 
#include <ctime>
#include <numeric>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#include "../ChannelData.hh"
#include "../HeaderData.hh"

#include "RedisData.hh"

// Implementing StreamDataMean
void daqAnalysis::StreamDataMean::Fill(unsigned instance_index, unsigned datum_index, float datum) {
  // not needed
  (void) datum_index;

  _instance_data[instance_index] += datum/_n_points_per_time[instance_index];
}

// clear data
void daqAnalysis::StreamDataMean::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
  }
  _n_values = 0;
}

float daqAnalysis::StreamDataMean::Data(unsigned index) {
  float ret = _data[index];
  return ret;
}

void daqAnalysis::StreamDataMean::Update() {
  // update instance data
  for (unsigned index = 0; index < _instance_data.size(); index++) {
    AddInstance(index);
  }
  // increment _n_values
  _n_values ++;
}

// takes new data value out of instance and puts it in _data (not idempotent)
void daqAnalysis::StreamDataMean::AddInstance(unsigned index) {
  // add instance data to data
  _data[index] = (_data[index] * _n_values + _instance_data[index]) / (_n_values + 1);

  // clear instance data
  _instance_data[index] = 0;
}

// Implementing StreamDataVariableMean
void daqAnalysis::StreamDataVariableMean::Fill(unsigned instance_index, unsigned datum_index, float datum) {
  // not needed
  (void) datum_index;

  // don't take values near zero
  if (datum < 1e-4) {
    return;
  }
  // add to the counter at this time instance
  _instance_data[instance_index] = (_n_values_current_instance[instance_index] * _instance_data[instance_index] + datum) 
      / (_n_values_current_instance[instance_index] + 1);
  _n_values_current_instance[instance_index] ++;
}

// takes new data value out of instance and puts it in _data (note: is idempotent)
void daqAnalysis::StreamDataVariableMean::AddInstance(unsigned index) {
  // ignore if there's no data at this time instance
  if (_n_values_current_instance[index] == 0) {
    return;
  }
  // add instance data to data
  _data[index] += (_instance_data[index] - _data[index]) / (_n_values[index] + 1);

  // clear instance data
  _instance_data[index] = 0;
  _n_values_current_instance[index] = 0;
  // increment _n_values if there was data
  _n_values[index] += 1;
}


float daqAnalysis::StreamDataVariableMean::Data(unsigned index) {
  // return data
  float ret = _data[index];
  return ret;
}

// clear data
void daqAnalysis::StreamDataVariableMean::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
    _n_values[index] = 0;
  }
}

void daqAnalysis::StreamDataVariableMean::Update() {
  // add instance data and increment n_values
  for (unsigned index = 0; index < _instance_data.size(); index++) {
    AddInstance(index);
  }
}

// Implementing StreamDataMax
void daqAnalysis::StreamDataMax::Fill(unsigned instance_index, unsigned datum_index, unsigned datum) {
  // not needed
  (void) datum_index;

  if (_data[instance_index] < datum) _data[instance_index] = datum;
}

unsigned daqAnalysis::StreamDataMax::Data(unsigned index) {
  float ret = _data[index];
  return ret;
}

// clear data
void daqAnalysis::StreamDataMax::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
  }
}

// Implementing StreamDataSum
void daqAnalysis::StreamDataSum::Fill(unsigned instance_index, unsigned datum_index, unsigned datum) {
  // not needed
  (void) datum_index;

  _data[instance_index] += datum;
}

unsigned daqAnalysis::StreamDataSum::Data(unsigned index) {
  float ret = _data[index];
  return ret;
}

// clear data
void daqAnalysis::StreamDataSum::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
  }
}

// Implementing StreamDataRMS
// uses Online RMS algorithm from: 
// Algorithms for Computing the Sample Variance: Analysis and Recommendations
// Tony Chan, Gene Golub, and Randall LeVeque
// The American Statistician
void daqAnalysis::StreamDataRMS::Fill(unsigned instance_index, unsigned datum_index, float datum) {
  // store previous mean
  float last_mean = 0;
  if (_n_values != 0) {
    last_mean = _means[instance_index].Data(datum_index); 
  }
  else {
    last_mean = datum;
  }
  // update mean
  _means[instance_index].Fill(datum_index, 0, datum);
  // get new mean
  float mean = _means[instance_index].Data(datum_index);
  _rms[instance_index][datum_index] += ( (datum - last_mean)*(datum - mean) - _rms[instance_index][datum_index]) / (_n_values + 1);
}

// clear data
void daqAnalysis::StreamDataRMS::Clear() {
  for (unsigned i = 0; i < _means.size(); i++) {
    _means[i].Clear();
    std::fill(_rms[i].begin(), _rms[i].end(), 0.);
  }
  _n_values = 0;
}

// get data
float daqAnalysis::StreamDataRMS::Data(unsigned index) {
  if (_n_values < 2) return 0;

  float sample_variance = std::accumulate(_rms[index].begin(), _rms[index].end(), 0.) / (_rms[index].size());
  float sample_rms = sqrt(sample_variance);
  return sample_rms;
}

// update data
void daqAnalysis::StreamDataRMS::Update() {
  for (unsigned i = 0; i < _means.size(); i++) {
    _means[i].Update();
  }
  _n_values ++;
}

// Defining string literal template parameters for inheritors of DetectorMetric
char REDIS_NAME_RMS[] = "rms";
char REDIS_NAME_OCCUPANCY[] = "hit_occupancy";
char REDIS_NAME_DNOISE[] = "next_channel_dnoise";
char REDIS_NAME_BASLINE[] = "baseline";
char REDIS_NAME_BASELINE_RMS[] = "baseline_rms";
char REDIS_NAME_PULSE_HEIGHT[] = "pulse_height";
char REDIS_NAME_RAWHIT_OCCUPANCY[] = "rawhit_occupancy";
char REDIS_NAME_RAWHIT_PULSE_HEIGHT[] = "rawhit_pulse_height";

// and of HeaderMetric
char REDIS_NAME_EVENT_NO[] = "event_no";
char REDIS_NAME_TRIG_FRAME_NO[] = "trig_frame_no";
char REDIS_NAME_FRAME_NO[] = "frame_no";
char REDIS_NAME_BLOCKS[] = "blocks";

//Eventinfo
char REDIS_NAME_PURITY[]="purity";
