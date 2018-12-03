#include <stdlib.h>
#include <stdio.h>
#include <cassert>
#include <ctime>
#include <numeric>
#include <chrono>
#include <iostream>
#include <string> 

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#include "art/Framework/Principal/Handle.h"

#include "lardataobj/RawData/RawDigit.h"

#include "../ChannelData.hh"
#include "../HeaderData.hh"
#include "../Noise.hh"
#include "../VSTChannelMap.hh"
#include "../FFT.hh"
#include "../EventInfo.hh"

#include "Redis.hh"
#include "RedisData.hh"

using namespace daqAnalysis;
using namespace std;

Redis::Redis(Redis::Config &config, daqAnalysis::VSTChannelMap *channel_map):
  _channel_map(channel_map),
  context(redisConnect(config.hostname.c_str(), 6379)),
  _now(std::time(nullptr)),
  _start(std::time(nullptr)),
  _snapshot_time(config.snapshot_time),
  _stream_take(config.stream_take),
  _stream_expire(config.stream_expire),
  _stream_last(config.NStreams(), 0),
  _stream_send(config.NStreams(), false),
  _sub_run_stream(config.sub_run_stream),
  _sub_run_stream_expire(config.sub_run_stream_expire),
  _n_streams(config.NStreams()),
  _this_subrun(0),
  _last_subrun(0),
  _this_run(0),
  _last_run(0),
  _last_snapshot(0),
  _first_run(true),

  // allocate and zero-initalize all of the metrics
  _rms(config.NStreams(), RedisRMS(channel_map)),
  _baseline(config.NStreams(), RedisBaseline(channel_map)),
  _baseline_rms(config.NStreams(), RedisBaselineRMS(channel_map)),
  _dnoise(config.NStreams(), RedisDNoise(channel_map)),
  _pulse_height(config.NStreams(), RedisPulseHeight(channel_map)),
  _occupancy(config.NStreams(), RedisOccupancy(channel_map)),
  _rawhit_pulse_height(config.NStreams(),RedisRawHitPulseHeight(channel_map)),
  _rawhit_occupancy(config.NStreams(), RedisRawHitOccupancy(channel_map)),

  // and the header stuff
  _frame_no(config.NStreams(), RedisFrameNo(channel_map)),
  _trig_frame_no(config.NStreams(), RedisTrigFrameNo(channel_map)),
  _event_no(config.NStreams(), RedisEventNo(channel_map)),
  _blocks(config.NStreams(), RedisBlocks(channel_map)),
  
  //event info
  _purity(config.NStreams(), RedisPurity(channel_map)), 

  _fft_manager((config.waveform_input_size > 0) ? config.waveform_input_size: 0),
  _do_timing(config.timing),
  _config(config)
{
  if (context != NULL && context->err) {
    std::cerr << "Redis error: " <<  context->errstr << std::endl;
    exit(1);
  }
}

Redis::~Redis() {
  //redisAsyncDisconnect(context);
  redisFree(context);
}

// flush the reamining data
void Redis::FlushData() {
  // don't flush if configured
  if (!_config.flush_data) return;

  // send the time data if you would have in the next second
  _now += 1;
  StartSend(_now, _this_run, _this_subrun);
  // but definitely send sub_run stream if there
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1;
    _stream_send[sub_run_ind] = true;
    _stream_last[sub_run_ind] = _this_subrun;
  }

  if (!_config.print_data) {
    SendChannelData();
    SendHeaderData();

    void *reply;
    reply = redisCommand(context, "SET last_subrun_no %u", _this_subrun);
    freeReplyObject(reply);
    
    // same with run
    reply = redisCommand(context, "SET this_run_no %u", _this_run);
    freeReplyObject(reply);
    
    // send Redis "Alive" signal
    reply = redisCommand(context, "SET MONITOR_%s_ALIVE %u", _config.monitor_name.c_str(), std::time(nullptr));
    freeReplyObject(reply);
  }
  else {
    PrintChannelData();
  }
}

void Redis::StartSend(unsigned run, unsigned sub_run) {
  StartSend(std::time(nullptr), run, sub_run);
}


void Redis::StartSend(uint64_t now, unsigned run, unsigned sub_run) {
  _now = now;

  // if stream last haven't been set yet, initialize them
  if (_stream_take.size() > 0) {
    if (_stream_last[0] == 0) {
      for (size_t i = 0; i < _stream_take.size(); i ++) {
        _stream_last[i] = _now;
      } 
    }
  }
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1; 
    if (_stream_last[sub_run_ind] == 0) {
      _stream_last[sub_run_ind] = sub_run;
    }
  }

  for (size_t i = 0; i < _stream_take.size(); i++) {
    // calculate whether each stream is sending to redis on this event
    _stream_send[i] = _stream_last[i] != _now && (_now - _stream_last[i]) >= _stream_take[i];
    if (_stream_send[i]) {
      _stream_last[i] = _now;
    }
  }
  // special-case the sub-run stream
  // this one is sent if we're on a new sub-run
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1;  
    _stream_send[sub_run_ind] = _stream_last[sub_run_ind] != sub_run; 
    if (_stream_send[sub_run_ind]) {
      _stream_last[sub_run_ind] = sub_run;
    }
  }
  _this_subrun = sub_run;
  _this_run = run;

  // update the run and subrun if its the first event
  if (_last_run == 0) _last_run = run;
  if (_last_subrun == 0) _last_subrun = sub_run;
  

}

void Redis::FinishSend() {
  if (_do_timing) {
    _timing.Print();
  }

  if (!_config.print_data) {
    // if a new subrun, set the value in redis
    if (_this_subrun != _last_subrun) {
      void *reply = redisCommand(context, "SET last_subrun_no %u", _last_subrun);
      freeReplyObject(reply);
    }

  // same with run
  if (_this_run != _last_run) {
    void *reply = redisCommand(context, "SET this_run_no %u", _this_run);
    freeReplyObject(reply);
  }

  // send Redis "Alive" signal
  void *reply = redisCommand(context, "SET MONITOR_%s_ALIVE %u", _config.monitor_name.c_str(), std::time(nullptr));
  freeReplyObject(reply);

  }
  
  _last_subrun = _this_subrun;
  _last_run = _this_run;

  _first_run = false;
}

void Redis::EventInfo(daqAnalysis::EventInfo *event_info) {
  //If -1 there has been a failure in the purity analysis and so we don't want to process that event. 
  if(event_info->purity != -1){
  SendEventInfo();
  FillEventInfo(event_info);
  }
}

void Redis::FillEventInfo(daqAnalysis::EventInfo *event_info){

  // update all of the metrics
  for (size_t i = 0; i < _n_streams; i++) {
    _purity[i].Fill(*event_info);
  }

  for (size_t i = 0; i < _n_streams; i++) {
    _purity[i].Update();
  }

}

void Redis::SendEventInfo(){
  if (_do_timing) {
    _timing.StartTime();
  }
  unsigned n_commands = 0;
  for (size_t i = 0; i < _n_streams; i++) {
      unsigned index = _now / _stream_take[i];
      const char *stream_name = std::to_string(_stream_take[i]).c_str(); 
      n_commands += _purity[i].Send(context, index, stream_name, _stream_expire[i]);
      _purity[i].Clear();
  }

  // special case the sub run stream
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1;  
    std::stringstream ss;
    ss << "sub_run_" << _this_run;
    const char *sub_run_ident = ss.str().c_str();

    // send headers to redis if need be
    if (_stream_send[sub_run_ind]) {
      n_commands += _purity[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);

      // the metric was taken iff it was sent to redis
      _purity[sub_run_ind].Clear();
    }
  }
  
  // actually send the commands out of the pipeline
  FinishPipeline(n_commands);
  
  if (_do_timing) {
    _timing.EndTime(&_timing.send_header_data);
  } 


}

void Redis::HeaderData(vector<daqAnalysis::HeaderData> *header_data) {
  if (!_config.print_data) {
    SendHeaderData();
  }
  FillHeaderData(header_data);
}

void Redis::FillHeaderData(vector<daqAnalysis::HeaderData> *header_data) {
  for (auto &header: *header_data) {
    // update header info in each stream
    for (size_t i = 0; i < _n_streams; i++) {
      // index into the fem data cache
      unsigned fem_ind = _channel_map->SlotIndex(header);
      _event_no[i].Fill(header, fem_ind);
      _frame_no[i].Fill(header, fem_ind);
      _trig_frame_no[i].Fill(header, fem_ind);
      _blocks[i].Fill(header, fem_ind);
    }
  }
  // update all of the metrics
  for (size_t i = 0; i < _n_streams; i++) {
    _event_no[i].Update();
    _frame_no[i].Update();
    _trig_frame_no[i].Update();
    _blocks[i].Update();
  }
}

void Redis::SendHeaderData() {
  if (_do_timing) {
    _timing.StartTime();
  }
  unsigned n_commands = 0;
  for (size_t i = 0; i < _stream_take.size(); i++) {
    // send headers to redis if need be
    if (_stream_send[i]) {
      uint64_t index = _now / _stream_take[i];
      const char *stream_name = std::to_string(_stream_take[i]).c_str(); 
      n_commands += _event_no[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _frame_no[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _trig_frame_no[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _blocks[i].Send(context, index, stream_name, _stream_expire[i]);
      // the metric was taken iff it was sent to redis
      _event_no[i].Clear();
      _frame_no[i].Clear();
      _trig_frame_no[i].Clear();
      _blocks[i].Clear();
    }
  }
  // special case the sub run stream
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1;  
    std::stringstream ss;
    ss << "sub_run_" << _this_run;
    const char *sub_run_ident = ss.str().c_str();

    // send headers to redis if need be
    if (_stream_send[sub_run_ind]) {
      n_commands += _event_no[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _frame_no[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _trig_frame_no[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _blocks[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);

      // the metric was taken iff it was sent to redis
      _event_no[sub_run_ind].Clear();
      _frame_no[sub_run_ind].Clear();
      _trig_frame_no[sub_run_ind].Clear();
      _blocks[sub_run_ind].Clear();
    }
  }
  
  // actually send the commands out of the pipeline
  FinishPipeline(n_commands);
  
  if (_do_timing) {
    _timing.EndTime(&_timing.send_header_data);
  } 

}

inline size_t pushFFTDat(char *buffer, float re, float im) {
  float dat = re*re + im*im;
  return sprintf(buffer, " %f", dat);
}

void Redis::Snapshot(vector<daqAnalysis::ChannelData> *per_channel_data, vector<NoiseSample> *noise, vector<vector<int>> *fem_summed_waveforms, 
    std::vector<std::vector<double>> *fem_summed_fft, const art::ValidHandle<std::vector<raw::RawDigit>> &digits, const std::vector<unsigned> &channel_to_index) {
  size_t n_commands = 0;

  // record the time for reference
  redisAppendCommand(context, "SET snapshot:time %i", _now);
  redisAppendCommand(context, "SET snapshot:sub_run %i", _this_subrun);
  redisAppendCommand(context, "SET snapshot:run %i", _this_run);
  n_commands += 3;

  if (_do_timing) _timing.StartTime();

  // stuff per fem
  if (fem_summed_waveforms->size() != 0) {
    for (unsigned fem_ind = 0; fem_ind < _channel_map->NFEM(); fem_ind++) {
      // sending the summed waveforms to redis
      // delete old list
      redisAppendCommand(context, "DEL snapshot:waveform:fem:%i", fem_ind);
      {
	// buffer for writing out waveform
	size_t buffer_len = (*fem_summed_waveforms)[fem_ind].size() * 10 + 50;
	char *buffer = new char[buffer_len];
	
	// print in the base of the command
	// assumes fem_summed_waveforms are already sorted by id
	size_t print_len = sprintf(buffer, "RPUSH snapshot:waveform:fem:%i", fem_ind);
	char *buffer_index = buffer + print_len;
	for (int dat: (*fem_summed_waveforms)[fem_ind]) {
	  print_len += sprintf(buffer_index, " %i", dat); 
 	  buffer_index = buffer + print_len;
	  if (print_len >= buffer_len - 1) {
            std::cerr << "ERROR: BUFFER OVERFLOW IN WAVEFORM DATA" << std::endl;
            std::exit(1);
          }
	}
	// submit the command
	redisAppendCommand(context, buffer);
	n_commands += 2;
	
	delete buffer;
      }
    }
  }
  // send fft's
  if (fem_summed_fft->size() != 0) {
    for (unsigned fem_ind = 0; fem_ind < _channel_map->NFEM(); fem_ind++) {
      // sending the ffts of summed waveforms to redis
      // delete old list
      redisAppendCommand(context, "DEL snapshot:fft:fem:%i", fem_ind);
      {
        // allocate buffer for fft storage command
        // FFT's are comprised of floats, which can get pretty big
        // so assume you need ~25 digits per float to be on the safe side
	size_t buffer_len = (*fem_summed_fft)[fem_ind].size() * 25 + 50;
	char *buffer = new char[buffer_len];
	
	// print in the base of the command
	// assumes fem_summed_fft is already sorted by id
	size_t print_len = sprintf(buffer, "RPUSH snapshot:fft:fem:%i", fem_ind);
	char *buffer_index = buffer + print_len;
	for (double dat: (*fem_summed_fft)[fem_ind]) {
	  print_len += sprintf(buffer_index, " %f", dat); 
 	  buffer_index = buffer + print_len;
	  if (print_len >= buffer_len - 1) {
            std::cerr << "ERROR: BUFFER OVERFLOW IN WAVEFORM DATA" << std::endl;
            std::exit(1);
          }
	}
	// submit the command
	redisAppendCommand(context, buffer);
	n_commands += 2;
	
	delete buffer;
      }
    }
  }

  if (_do_timing) _timing.EndTime(&_timing.fem_waveforms);

  // stuff per channel
  for (auto &channel: *per_channel_data) { 
    if (_do_timing) {
      _timing.StartTime();
    }
    // store the waveform and fft's
    // also delete old lists
    redisAppendCommand(context, "DEL snapshot:waveform:wire:%i", channel.channel_no);
    
    // we're gonna put the whole waveform into one very large list 
    // allocate enough space for it 
    // Assume at max 4 chars per int plus a space each plus another 50 chars to store the base of the command
    {
      unsigned digits_ind = channel_to_index[channel.channel_no];
      auto waveform = (*digits)[digits_ind].ADCs();
      size_t buffer_len = waveform.size() * 10 + 50;
      char *buffer = new char[buffer_len];

      // print in the base of the command
      size_t print_len = sprintf(buffer, "RPUSH snapshot:waveform:wire:%i", channel.channel_no);
      char *buffer_index = buffer + print_len;
      // throw in all of the data points
      for (int16_t dat: waveform) {
	/* IGNORE: Possible evil way of pushing stuff to redis to be possibly used later */
	// add the space from the previous command
	//*buffer_index = ' ';
	//print_len ++;
	//buffer_index = buffer + print_len;
	
	// add the number in binary:
	// coerce the integer into a char[2]:
	//char *binary_dat = (char *) &dat;
	// to avoid null-termination, mask w/ 
	//buffer_index[0] = binary_dat[0];
	//buffer_index[1] = binary_dat[1];
	
	// 2 bytes
	//print_len += 2;
	
	print_len += sprintf(buffer_index, " %i", dat); 
	
	buffer_index = buffer + print_len;
	
	if (print_len >= buffer_len - 1) {
          std::cerr << "ERROR: BUFFER OVERFLOW IN WAVEFORM DATA" << std::endl;
          std::exit(1);
        }
      }
      // null terminate the string
      //*buffer_index = '\0';
      
      redisAppendCommand(context, buffer);
      n_commands += 2;
      // delete the buffer
      delete buffer;
    }

    if (_do_timing) {
      _timing.EndTime(&_timing.send_waveform);
    }

    if (_do_timing) {
      _timing.StartTime();
    }

    redisAppendCommand(context, "DEL snapshot:fft:wire:%i", channel.channel_no);

    {
      unsigned digits_ind = channel_to_index[channel.channel_no];
      auto waveform = (*digits)[digits_ind].ADCs();
      // allocate buffer for fft storage command
      // FFT's are comprised of floats, which can get pretty big
      // so assume you need ~25 digits per float to be on the safe side
      size_t buffer_len = (waveform.size()/2 +1) * 25 + 50;
      char *buffer = new char[buffer_len];
      
      // print in the base of the command
      size_t print_len = sprintf(buffer, "RPUSH snapshot:fft:wire:%i", channel.channel_no);
      char *buffer_index = buffer + print_len;
      // throw in all of the data points
      
      // use already calculated FFT if there
      if (channel.fft_real.size() != 0) {
        for (size_t i = 0; i < channel.fft_real.size(); i++) {
          print_len += pushFFTDat(buffer_index, channel.fft_real[i], channel.fft_imag[i]);
          buffer_index = buffer + print_len;
        }
      }
      // otherwise calculate it yourself
      else {
        // re-allocate if necessary
        if (_fft_manager.InputSize() != waveform.size()) {
          _fft_manager.Set(waveform.size());
        }
        // fill up the fft data in
        for (size_t i = 0; i < waveform.size(); i++) {
          double *input = _fft_manager.InputAt(i);
          *input = (double) waveform[i];
        }
        _fft_manager.Execute();
        int adc_fft_size = _fft_manager.OutputSize();
        for (int i = 0; i < adc_fft_size; i++) {
          print_len += pushFFTDat(buffer_index, _fft_manager.ReOutputAt(i), _fft_manager.ImOutputAt(i));
          buffer_index = buffer + print_len;
        }
      }
      redisAppendCommand(context, buffer);
      n_commands += 2;
      // delete the buffer
      delete buffer;
    }

    if (_do_timing) {
      _timing.EndTime(&_timing.send_fft);
    }
  }

  if (_do_timing) {
    _timing.StartTime();
  }
  // also store the noise correlation matrix if taking a snapshot
  redisAppendCommand(context, "DEL snapshot:correlation");
  
  {
    // allocate for the covariance matrix command
    // again, get ~25 digits for each float
    size_t buffer_len = ((noise->size() + 1) * noise->size() / 2) * 25 + 50;
    char *buffer = new char[buffer_len];
    
    // print in the base of the command
    size_t print_len = sprintf(buffer, "RPUSH snapshot:correlation");
    char *buffer_index = buffer + print_len;
    // Only calculate the upper-right half of the matrix since it is symmetric
    // The index 'k' into the list of the i-th sample with the j-th sample (where i <= j) is:
    // k = ((n+1)*n/2) - (n-i+1)*(n-i)/2 + j - i
    for (size_t i = 0; i < noise->size(); i++) {
      for (size_t j = i; j < noise->size(); j++) {
        unsigned digits_i = channel_to_index[i];
        unsigned digits_j = channel_to_index[j];
        auto waveform_i = (*digits)[digits_i].ADCs();
        auto waveform_j = (*digits)[digits_j].ADCs();

        float correlation = (*noise)[i].Correlation(waveform_i, (*noise)[j], waveform_j);
        print_len += sprintf(buffer_index, " %f", correlation);
        buffer_index = buffer + print_len;
      }
    }
    redisAppendCommand(context, buffer);
    n_commands += 2;
    delete buffer;
  }

  if (_do_timing) {
    _timing.EndTime(&_timing.correlation);
  }

  if (_do_timing) {
    _timing.StartTime();
  }
  FinishPipeline(n_commands);
  if (_do_timing) {
    _timing.EndTime(&_timing.clear_pipeline);
  }
  
}

bool Redis::WillTakeSnapshot() {
  int64_t time_diff = ((int)_now - _last_snapshot);
  return _snapshot_time > 0 && time_diff >= _snapshot_time && _last_snapshot != _now;
}

void Redis::ChannelData(vector<daqAnalysis::ChannelData> *per_channel_data, vector<NoiseSample> *noise_samples, vector<vector<int>> *fem_summed_waveforms, 
    std::vector<std::vector<double>> *fem_summed_fft, const art::ValidHandle<std::vector<raw::RawDigit>> &digits, const std::vector<unsigned> &channel_to_index) {

  if (!_config.print_data) {
    SendChannelData();
  }
  else {
    PrintChannelData();
  }
  FillChannelData(per_channel_data);

  int64_t time_diff = ((int)_now - _last_snapshot);
  bool take_snapshot = _snapshot_time > 0 && time_diff >= _snapshot_time && _last_snapshot != _now;
  if (take_snapshot) {
    Snapshot(per_channel_data, noise_samples, fem_summed_waveforms, fem_summed_fft, digits, channel_to_index);
    _last_snapshot = _now;
  }

}

void Redis::FillChannelData(vector<daqAnalysis::ChannelData> *per_channel_data) {
  if (_do_timing) {
    _timing.StartTime();
  }

  // iterate over crates and fems
  for (unsigned crate = 0; crate < _channel_map->NCrates(); crate++) {
    for (unsigned fem = 0; fem < _channel_map->NFEM(); fem++) {
      // @VST INSTALLATION: OK -- crate is always 0
      // index into the fem data cache
      unsigned fem_ind = fem;

      for (unsigned channel = 0; channel < _channel_map->NSlotChannel(); channel ++) {
        if (!_channel_map->IsMappedChannel(channel, fem, crate, true)) continue;

        // get the wire number
	uint16_t wire = _channel_map->Channel2Wire(channel, fem, crate, true);

        // get index of channel on fem
        unsigned fem_channel_ind = _channel_map->ReadoutChannel2FEMInd(channel, fem, crate, true);

        // since there is only one crate, we can use the wire id as the crate index
        unsigned crate_channel_ind = wire;
 
        // fill metrics for each stream
        for (size_t i = 0; i < _n_streams; i++) {
          _rms[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
          _baseline[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
          _baseline_rms[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
          _dnoise[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
          _pulse_height[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
	  _rawhit_pulse_height[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
          _occupancy[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
	  _rawhit_occupancy[i].Fill((*per_channel_data)[wire], crate, crate_channel_ind, fem_ind, fem_channel_ind, wire);
        }

      }

    }
  }
  if (_do_timing) {
    _timing.EndTime(&_timing.copy_data);
  }

  // update all of the metrics
  for (size_t i = 0; i < _n_streams; i++) {
    _rms[i].Update();
    _baseline[i].Update();
    _baseline_rms[i].Update();
    _dnoise[i].Update();
    _pulse_height[i].Update();
    _rawhit_pulse_height[i].Update();
    _occupancy[i].Update();
    _rawhit_occupancy[i].Update();
  }
}

void Redis::PrintChannelData() {
  for (size_t i = 0; i < _stream_take.size(); i++) {
    if (_stream_send[i]) {
      const char *stream_name = std::to_string(_stream_take[i]).c_str(); 
      _rms[i].Print(stream_name);
      _baseline[i].Print(stream_name);
      _baseline_rms[i].Print(stream_name);
      _dnoise[i].Print(stream_name);
      _occupancy[i].Print(stream_name);
      _pulse_height[i].Print(stream_name);
      _rawhit_occupancy[i].Print(stream_name);
      _rawhit_pulse_height[i].Print(stream_name);
    }
  }

  // special case the sub run stream
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1;  
    if (_stream_send[sub_run_ind]) {
    
      std::stringstream ss;
      ss << "sub_run_" << _this_run;
      const char *stream_name = ss.str().c_str();
      _rms[sub_run_ind].Print(stream_name);
      _baseline[sub_run_ind].Print(stream_name);
      _baseline_rms[sub_run_ind].Print(stream_name);
      _dnoise[sub_run_ind].Print(stream_name);
      _occupancy[sub_run_ind].Print(stream_name);
      _pulse_height[sub_run_ind].Print(stream_name);
      _rawhit_occupancy[sub_run_ind].Print(stream_name);
      _rawhit_pulse_height[sub_run_ind].Print(stream_name);
    }
  }
}

void Redis::SendChannelData() {
  unsigned n_commands = 0;
  for (size_t i = 0; i < _stream_take.size(); i++) {
    // Send stuff to redis if it's time
    if (_stream_send[i]) {
      if (_do_timing) {
        _timing.StartTime();
      }
      uint64_t index = _now / _stream_take[i];
      const char *stream_name = std::to_string(_stream_take[i]).c_str(); 
      // metrics control the sending of everything else
      n_commands += _rms[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _baseline[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _baseline_rms[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _dnoise[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _occupancy[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _pulse_height[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _rawhit_occupancy[i].Send(context, index, stream_name, _stream_expire[i]);
      n_commands += _rawhit_pulse_height[i].Send(context, index, stream_name, _stream_expire[i]);

      _rms[i].Clear();
      _baseline[i].Clear();
      _baseline_rms[i].Clear();
      _dnoise[i].Clear();
      _pulse_height[i].Clear();
      _occupancy[i].Clear();
      _rawhit_pulse_height[i].Clear();
      _rawhit_occupancy[i].Clear();

      if (_do_timing) {
        _timing.EndTime(&_timing.send_metrics);
      }
    }
  }
  // special case the sub run stream
  if (_sub_run_stream) {
    unsigned sub_run_ind = _n_streams - 1;  
    // Send stuff to redis if it's time
    if (_stream_send[sub_run_ind]) {
      if (_do_timing) {
        _timing.StartTime();
      }

      std::stringstream ss;
      ss << "sub_run_" << _this_run;
      const char *sub_run_ident = ss.str().c_str();

      // metrics control the sending of everything else
      n_commands += _rms[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _baseline[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _baseline_rms[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _dnoise[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _occupancy[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _pulse_height[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _rawhit_occupancy[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);
      n_commands += _rawhit_pulse_height[sub_run_ind].Send(context, _last_subrun, sub_run_ident, _sub_run_stream_expire);

      // the metric was taken iff it was sent to redis
      // clear all of the metrics
      _rms[sub_run_ind].Clear();
      _baseline[sub_run_ind].Clear();
      _baseline_rms[sub_run_ind].Clear();
      _dnoise[sub_run_ind].Clear();
      _pulse_height[sub_run_ind].Clear();
      _occupancy[sub_run_ind].Clear();
      _rawhit_pulse_height[sub_run_ind].Clear();
      _rawhit_occupancy[sub_run_ind].Clear();

      if (_do_timing) {
        _timing.EndTime(&_timing.send_metrics);
      }
    }
  }

  // actually send the commands out of the pipeline
  FinishPipeline(n_commands);
}

// clear out a sequence of Append Commands
void Redis::FinishPipeline(size_t n_commands) {
  // TODO: Error Handling
  void *reply;
  for (size_t i =0; i <n_commands; i++) {
    redisGetReply(context, &reply); 
    freeReplyObject(reply);
  }
}

void RedisTiming::StartTime() {
  start = std::chrono::high_resolution_clock::now(); 
}
void RedisTiming::EndTime(float *field) {
  auto now = std::chrono::high_resolution_clock::now();
  *field += std::chrono::duration<float, std::milli>(now- start).count();
}
void RedisTiming::Print() {
  std::cout << "COPY DATA: " << copy_data << std::endl;
  std::cout << "SEND METRICS: " << send_metrics << std::endl;
  std::cout << "SEND HEADER : " << send_header_data << std::endl;
  std::cout << "SEND WAVEFORM " << send_waveform << std::endl;
  std::cout << "SEND FFT    : " << send_fft << std::endl;
  std::cout << "CORRELATION : " << correlation << std::endl;
  std::cout << "CLEAR PIPE  : " << clear_pipeline << std::endl;
  std::cout << "FEM WAVEFORM: " << fem_waveforms << std::endl;
}

