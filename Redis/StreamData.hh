#ifndef StreamData_h
#define StreamData_h

#include <vector>
#include <ctime>
#include <cmath>
#include <iostream>
#include <stdlib.h>

namespace sbndOnline { 
  // stream interface
  template<typename DataType>
  class StreamData;

  // stream types
  class StreamDataMean;
  class StreamDataVariableMean;
  class StreamDataMax;
  class StreamDataSum;
  class StreamDataRMS;
}


// virtual class which defines the interface that all StreamData classses must follow
template<typename DataType>
class sbndOnline::StreamData {
public:
  // add in a new value
  virtual void Fill(unsigned instance_index, unsigned datum_index, DataType datum) = 0;
  // clear values
  virtual void Clear() = 0;
  // get data value
  virtual DataType Data(unsigned index) = 0;
  // get number of data points
  virtual unsigned Size() = 0;
  // call per instance
  virtual void Update() = 0;
  // set points per time of an instance_index
  virtual void SetPointsPerTime(unsigned index, unsigned n_points) = 0;
  // virtual destructor to avoid memory leaks
  virtual ~StreamData() = 0;
}

// keeps a running mean of a metric w/ n_data instances and n_points_per_time data points per each time instance
class sbndOnline::StreamDataMean: public sbndOnline::SteamData<float> {
public:
  StreamDataMean(unsigned n_data, unsigned n_points_per_time): _data(n_data, 0.), _n_values(0), _instance_data(n_data, 0.), _n_points_per_time(n_data, n_points_per_time) {}

  // add in a new value
  void Fill(unsigned instance_index, unsigned datum_index, float datum) override;
  // clear values
  void Clear() override;
  // take the data value and reset it
  float Data(unsigned index) override;
  // returns n_data
  unsigned Size() override { return _data.size(); }
  // called per iter
  void Update() override;
  // update a points per time value
  void SetPointsPerTime(unsigned index, unsigned points) override
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
class sbndOnline::StreamDataVariableMean: public sbndOnline::SteamData<float> {
public:
  // n_points_per_time is not used in this class
  StreamDataVariableMean(unsigned n_data, unsigned _): _data(n_data, 0.), _n_values(n_data, 0), _instance_data(n_data, 0), _n_values_current_instance(n_data,0) {}

  // add in a new value
  void Fill(unsigned instance_index, unsigned datum_index, float datum) override;
  // take the data value and reset it
  float Data(unsigned index) override;
  // returns n_data
  unsigned Size() override { return _data.size(); }
  // called per iter
  void Update() override;
  // clear values
  void Clear() override;

  // update a points per time value
  // does nothing, since all points per time values are dynamic for this class
  void SetPointsPerTime(unsigned index, unsigned points) override {}

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
class sbndOnline::StreamDataMax: public sbndOnline::SteamData<unsigned> {
public:
  StreamDataMax(unsigned n_data, unsigned _): _data(n_data, 0) {}

  void Fill(unsigned instance_index, unsigned datum_index, unsigned datum) override;
  unsigned Data(unsigned index) override;
  unsigned Size() override { return _data.size(); }
  void Update() override {/* doesn't need to do anything currently */}
  // clear values
  void Clear() override;

  // doesn't need to do anything currently
  void SetPointsPerTime(unsigned index, unsigned points) override {}

protected:
  std::vector<unsigned> _data;
};

// keeps running sum of a metric w/ n instances
class sbndOnline::StreamDataSum: public sbndOnline::SteamData<unsigned> {
public:
  StreamDataSum(unsigned n_data, unsigned _): _data(n_data, 0) {}

  void Fill(unsigned instance_index, unsigned datum_index, unsigned datum) override;
  unsigned Data(unsigned index) override;
  unsigned Size() override { return _data.size(); }
  void Update() override {/* doesn't need to do anything currently */}
  // clear values
  void Clear() override ;

  // doesn't need to do anything currently
  void SetPointsPerTime(unsigned index, unsigned points) override {}

protected:
  std::vector<unsigned> _data;
};

// keeps track of running RMS value 
class sbndOnline::StreamDataRMS: public sbndOnline::SteamData<float> {
public:
  StreamDataRMS(unsigned n_data, unsigned n_points_per_time): 
    _means(n_data, StreamDataMean(n_points_per_time, 1)), 
    _rms(n_data, std::vector<float>(n_points_per_time)), 
    _n_values(0) 
  {}

  // add in a new value
  void Fill(unsigned instance_index, unsigned datum_index, float datum) override;
  // clear values
  void Clear() override;
  // take the data value and reset it
  float Data(unsigned index) override;
  // returns n_data
  unsigned Size() override { return _rms.size(); }
  // called per iter
  void Update() override;
  // update a points per time value
  void SetPointsPerTime(unsigned index, unsigned points) override {
    _means[index] = StreamDataMean(points, 1);
    _rms[index].resize(points);
  }

protected:
  // internal data
  std::vector<sbndOnline::StreamDataMean> _means;
  std::vector<std::vector<float>> _rms;
  unsigned _n_values;
};

#endif
