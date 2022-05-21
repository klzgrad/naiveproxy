// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "bidirectional_stream_c.h"
#include "cronet_c.h"

class BidirectionalStreamCallback {
 public:
  bidirectional_stream* stream = nullptr;
  char read_buffer[10240];
  std::atomic<bool> done = false;

  bidirectional_stream_callback* callback() const { return &s_callback; }

 private:
  static BidirectionalStreamCallback* FromStream(bidirectional_stream* stream) {
    return reinterpret_cast<BidirectionalStreamCallback*>(stream->annotation);
  }

  // C callbacks.
  static void on_stream_ready_callback(bidirectional_stream* stream) {
    puts("on_stream_ready_callback");
  }

  static void on_response_headers_received_callback(
      bidirectional_stream* stream,
      const bidirectional_stream_header_array* headers,
      const char* negotiated_protocol) {
    printf("on_response_headers_received_callback negotiated_protocol=%s\n",
           negotiated_protocol);
    BidirectionalStreamCallback* self = FromStream(stream);
    for (size_t i = 0; i < headers->count; ++i) {
      if (headers->headers[i].key[0] == '\0')
        continue;
      printf("%s: %s\n", headers->headers[i].key, headers->headers[i].value);
    }
    bidirectional_stream_read(stream, self->read_buffer,
                              sizeof(self->read_buffer));
  }

  static void on_read_completed_callback(bidirectional_stream* stream,
                                         char* data,
                                         int count) {
    printf("on_read_completed_callback %d\n", count);
    BidirectionalStreamCallback* self = FromStream(stream);
    if (count == 0)
      return;
    fwrite(data, 1, count, stdout);
    puts("");
    bidirectional_stream_read(stream, self->read_buffer,
                              sizeof(self->read_buffer));
  }

  static void on_write_completed_callback(bidirectional_stream* stream,
                                          const char* data) {
    puts("on_write_completed_callback");
  }

  static void on_response_trailers_received_callback(
      bidirectional_stream* stream,
      const bidirectional_stream_header_array* trailers) {
    puts("on_response_trailers_received_callback");
    for (size_t i = 0; i < trailers->count; ++i) {
      printf("%s: %s\n", trailers->headers[i].key, trailers->headers[i].value);
    }
  }

  static void on_succeded_callback(bidirectional_stream* stream) {
    puts("on_succeded_callback");
    BidirectionalStreamCallback* self = FromStream(stream);
    self->done = true;
  }

  static void on_failed_callback(bidirectional_stream* stream, int net_error) {
    printf("on_failed_callback %d\n", net_error);
    BidirectionalStreamCallback* self = FromStream(stream);
    self->done = true;
  }

  static void on_canceled_callback(bidirectional_stream* stream) {
    puts("on_canceled_callback");
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
  Cronet_EngineParams_user_agent_set(engine_params, "Cronet");
  Cronet_EngineParams_experimental_options_set(engine_params, R"({
  "ssl_key_log_file": "/tmp/keys",
  "feature_list": {
    "enable-features": "PartitionConnectionsByNetworkIsolationKey"
  },
  "socket_limits": {
    "max_sockets_per_pool": { "NORMAL_SOCKET_POOL": 1024 },
    "max_sockets_per_proxy_server": {"NORMAL_SOCKET_POOL": 1024 },
    "max_sockets_per_group": { "NORMAL_SOCKET_POOL": 1024 }
  },
  "proxy_server": "socks5://127.0.0.1:1080"
})");
  Cronet_Engine_StartWithParams(cronet_engine, engine_params);
  Cronet_EngineParams_Destroy(engine_params);
  return cronet_engine;
}

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    printf("Usage: %s url\n", argv[0]);
    return 1;
  }
  const char* url = argv[1];
  Cronet_EnginePtr cronet_engine = CreateCronetEngine();
  stream_engine* cronet_stream_engine =
      Cronet_Engine_GetStreamEngine(cronet_engine);

  Cronet_Engine_StartNetLogToFile(cronet_engine, "/tmp/log.json", true);
  BidirectionalStreamCallback stream_callback;
  stream_callback.stream = bidirectional_stream_create(
      cronet_stream_engine, &stream_callback, stream_callback.callback());

  bidirectional_stream_header headers[] = {
      {"-network-isolation-key", "http://a"},
  };
  const bidirectional_stream_header_array headers_array = {1, 1, headers};
  if (bidirectional_stream_start(stream_callback.stream, url, 0, "GET",
                                 &headers_array, true) < 0) {
    stream_callback.done = true;
  }
  puts("bidirectional_stream_start");
  while (!stream_callback.done) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  bidirectional_stream_destroy(stream_callback.stream);

  Cronet_Engine_StopNetLog(cronet_engine);
  Cronet_Engine_Shutdown(cronet_engine);
  Cronet_Engine_Destroy(cronet_engine);
  return 0;
}
