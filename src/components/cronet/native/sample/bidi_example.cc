// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// g++ bidi_example.cc libcronet.100.0.4896.60.so
// LD_LIBRARY_PATH=$PWD ./a.out
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

#include "bidirectional_stream_c.h"
#include "cronet_c.h"

class BidirectionalStreamCallback {
 public:
  bidirectional_stream* stream = nullptr;
  std::string write_data;
  std::string read_buffer;
  std::atomic<bool> done = false;

  bidirectional_stream_callback* callback() const { return &s_callback; }

 private:
  static BidirectionalStreamCallback* FromStream(bidirectional_stream* stream) {
    return reinterpret_cast<BidirectionalStreamCallback*>(stream->annotation);
  }

  // C callbacks.
  static void on_stream_ready_callback(bidirectional_stream* stream) {
    std::cout << "on_stream_ready_callback" << std::endl;
    BidirectionalStreamCallback* self = FromStream(stream);
    bidirectional_stream_write(stream, self->write_data.data(),
                               self->write_data.size(), true);
    bidirectional_stream_flush(stream);
  }

  static void on_response_headers_received_callback(
      bidirectional_stream* stream,
      const bidirectional_stream_header_array* headers,
      const char* negotiated_protocol) {
    std::cout << "on_response_headers_received_callback negotiated_protocol="
              << negotiated_protocol << "\n";
    BidirectionalStreamCallback* self = FromStream(stream);
    for (size_t i = 0; i < headers->count; ++i) {
      if (headers->headers[i].key[0] == '\0')
        continue;
      std::cout << headers->headers[i].key << ": " << headers->headers[i].value
                << std::endl;
    }
    self->read_buffer.resize(32768);
    bidirectional_stream_read(stream, self->read_buffer.data(),
                              self->read_buffer.size());
  }

  static void on_read_completed_callback(bidirectional_stream* stream,
                                         char* data,
                                         int count) {
    std::cout << "on_read_completed_callback " << count << "\n";
    BidirectionalStreamCallback* self = FromStream(stream);
    if (count == 0)
      return;
    std::cout.write(data, count);
    std::cout << "\n";
    bidirectional_stream_read(stream, self->read_buffer.data(),
                              self->read_buffer.size());
  }

  static void on_write_completed_callback(bidirectional_stream* stream,
                                          const char* data) {
    std::cout << "on_write_completed_callback\n";
  }

  static void on_response_trailers_received_callback(
      bidirectional_stream* stream,
      const bidirectional_stream_header_array* trailers) {
    std::cout << "on_response_trailers_received_callback\n";
    for (size_t i = 0; i < trailers->count; ++i) {
      std::cout << trailers->headers[i].key << ": "
                << trailers->headers[i].value << std::endl;
    }
  }

  static void on_succeded_callback(bidirectional_stream* stream) {
    std::cout << "on_succeded_callback\n";
    BidirectionalStreamCallback* self = FromStream(stream);
    self->done = true;
  }

  static void on_failed_callback(bidirectional_stream* stream, int net_error) {
    std::cout << "on_failed_callback " << net_error << "\n";
    BidirectionalStreamCallback* self = FromStream(stream);
    self->done = true;
  }

  static void on_canceled_callback(bidirectional_stream* stream) {
    std::cout << "on_canceled_callback\n";
    BidirectionalStreamCallback* self = FromStream(stream);
    self->done = true;
  }

  static bidirectional_stream_callback s_callback;
};

bidirectional_stream_callback BidirectionalStreamCallback::s_callback = {
    on_stream_ready_callback,
    on_response_headers_received_callback,
    on_read_completed_callback,
    on_write_completed_callback,
    on_response_trailers_received_callback,
    on_succeded_callback,
    on_failed_callback,
    on_canceled_callback,
};

Cronet_EnginePtr CreateCronetEngine() {
  Cronet_EnginePtr cronet_engine = Cronet_Engine_Create();
  Cronet_EngineParamsPtr engine_params = Cronet_EngineParams_Create();
  Cronet_EngineParams_user_agent_set(engine_params, "CronetSample/1");
  Cronet_EngineParams_experimental_options_set(engine_params, R"(
    {"ssl_key_log_file": "/tmp/keys"}
)");

  Cronet_Engine_StartWithParams(cronet_engine, engine_params);
  Cronet_EngineParams_Destroy(engine_params);
  return cronet_engine;
}

int main(int argc, const char* argv[]) {
  if (argc != 3) {
    std::cout << "Usage: " << argv[0]
              << " https://my-caddy-forwardproxy.com \"Basic $(printf "
                 "user:pass | base64)\"\n";
    return 1;
  }
  const char* proxy_server = argv[1];
  const char* password_base64 = argv[2];
  Cronet_EnginePtr cronet_engine = CreateCronetEngine();
  stream_engine* cronet_stream_engine =
      Cronet_Engine_GetStreamEngine(cronet_engine);

  BidirectionalStreamCallback stream_callback;
  stream_callback.write_data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
  stream_callback.stream = bidirectional_stream_create(
      cronet_stream_engine, &stream_callback, stream_callback.callback());
  bidirectional_stream_header headers[2] = {
      {"proxy-authorization", password_base64},
      {"real-authority", "example.com:80"},
  };
  bidirectional_stream_header_array header_array = {2, 2, headers};
  bidirectional_stream_start(stream_callback.stream, proxy_server, 0, "CONNECT",
                             &header_array, false);
  std::cout << "bidirectional_stream_start" << std::endl;
  while (!stream_callback.done) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  bidirectional_stream_destroy(stream_callback.stream);

  Cronet_Engine_Shutdown(cronet_engine);
  Cronet_Engine_Destroy(cronet_engine);
  return 0;
}
