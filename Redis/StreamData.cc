#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <cassert> 
#include <ctime>
#include <numeric>

// Implementing StreamDataMean
void sbndOnline::StreamDataMean::Fill(unsigned instance_index, unsigned datum_index, float datum) {
  // not needed
  (void) datum_index;

  _instance_data[instance_index] += datum/_n_points_per_time[instance_index];
}

// clear data
void sbndOnline::StreamDataMean::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
  }
  _n_values = 0;
}

float sbndOnline::StreamDataMean::Data(unsigned index) {
  float ret = _data[index];
  return ret;
}

void sbndOnline::StreamDataMean::Update() {
  // update instance data
  for (unsigned index = 0; index < _instance_data.size(); index++) {
    AddInstance(index);
  }
  // increment _n_values
  _n_values ++;
}

// takes new data value out of instance and puts it in _data (not idempotent)
void sbndOnline::StreamDataMean::AddInstance(unsigned index) {
  // add instance data to data
  _data[index] = (_data[index] * _n_values + _instance_data[index]) / (_n_values + 1);

  // clear instance data
  _instance_data[index] = 0;
}

// Implementing StreamDataVariableMean
void sbndOnline::StreamDataVariableMean::Fill(unsigned instance_index, unsigned datum_index, float datum) {
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
void sbndOnline::StreamDataVariableMean::AddInstance(unsigned index) {
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


float sbndOnline::StreamDataVariableMean::Data(unsigned index) {
  // return data
  float ret = _data[index];
  return ret;
}

// clear data
void sbndOnline::StreamDataVariableMean::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
    _n_values[index] = 0;
  }
}

void sbndOnline::StreamDataVariableMean::Update() {
  // add instance data and increment n_values
  for (unsigned index = 0; index < _instance_data.size(); index++) {
    AddInstance(index);
  }
}

// Implementing StreamDataMax
void sbndOnline::StreamDataMax::Fill(unsigned instance_index, unsigned datum_index, unsigned datum) {
  // not needed
  (void) datum_index;

  if (_data[instance_index] < datum) _data[instance_index] = datum;
}

unsigned sbndOnline::StreamDataMax::Data(unsigned index) {
  float ret = _data[index];
  return ret;
}

// clear data
void sbndOnline::StreamDataMax::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
  }
}

// Implementing StreamDataSum
void sbndOnline::StreamDataSum::Fill(unsigned instance_index, unsigned datum_index, unsigned datum) {
  // not needed
  (void) datum_index;

  _data[instance_index] += datum;
}

unsigned sbndOnline::StreamDataSum::Data(unsigned index) {
  float ret = _data[index];
  return ret;
}

// clear data
void sbndOnline::StreamDataSum::Clear() {
  for (unsigned index = 0; index < _data.size(); index++) {
    _data[index] = 0;
  }
}

// Implementing StreamDataRMS
// uses Online RMS algorithm from: 
// Algorithms for Computing the Sample Variance: Analysis and Recommendations
// Tony Chan, Gene Golub, and Randall LeVeque
// The American Statistician
void sbndOnline::StreamDataRMS::Fill(unsigned instance_index, unsigned datum_index, float datum) {
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
void sbndOnline::StreamDataRMS::Clear() {
  for (unsigned i = 0; i < _means.size(); i++) {
    _means[i].Clear();
    std::fill(_rms[i].begin(), _rms[i].end(), 0.);
  }
  _n_values = 0;
}

// get data
float sbndOnline::StreamDataRMS::Data(unsigned index) {
  if (_n_values < 2) return 0;

  float sample_variance = std::accumulate(_rms[index].begin(), _rms[index].end(), 0.) / (_rms[index].size());
  float sample_rms = sqrt(sample_variance);
  return sample_rms;
}

// update data
void sbndOnline::StreamDataRMS::Update() {
  for (unsigned i = 0; i < _means.size(); i++) {
    _means[i].Update();
  }
  _n_values ++;
}
