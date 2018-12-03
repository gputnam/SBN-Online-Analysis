#ifndef RedisData_h
#define RedisData_h

#include <vector>
#include <ctime>
#include <cmath>
#include <iostream>
#include <stdlib.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

namespace sbndOnline {
  // defines interface for sending StreamData to some source
  class OutputSource;

  // class holding information to gneerate an index for a data stream
  class StreamDataIndex;

  // implements OutputSource for Redis
  class RedisOut;

  // implements OutputSoruce for pinting to stdout
  class PrintOut;
}


class sbndOnline::OutputSource {
public:
  virtual void Send(const char *stream_name, const char *index_name, const char *metric_name, float data, unsigned expire) = 0; 
  virtual ~OutputSource() = 0;
}

class sbndOnline::RedisOut: public sbndOnline::OutputSource {
public:
  explicit RedisOut(redisContext *context): _context(context) {}
 
  void Send(const char *stream_name, const char *index_name, const char *metric_name, float data, unsigned expire) override {
    redisAppendCommand(_context, "SET stream:%s:%s:%s %f", stream_name, index_name, metric_name, data);
    if (expire != 0) {
      redisAppendCommand(_context, "EXPIRE stream:%s:%s:%s %u", stream_name, index_name, metric_name, expire); 
    }
  }

private:
  redisContext *context;
}

class sbndOnline::PrintOut: public sbndOnline::OutputSource {
public:
  void Send(const char *stream_name, const char *index_name, const char *metric_name, float data, unsigned _ /* ignore expire */) override {
    std::cout << "STREAM: " << stream_name << " AT INDEX: " << index_name << " AT LOCATION: " << metric_name << " : " << data << std::endl;
  }
}

class sbndOnline::StreamDataIndex {
public:
  unsigned run;
  unsigned sub_run;
  unsigned event;
  unsigned unix_time;
}
#endif
