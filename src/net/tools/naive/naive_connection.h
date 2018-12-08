// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CONNECTION_H_
#define NET_TOOLS_NAIVE_NAIVE_CONNECTION_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"

namespace net {

class ClientSocketHandle;
class DrainableIOBuffer;
class IOBuffer;
class StreamSocket;
struct NetworkTrafficAnnotationTag;

class NaiveConnection {
 public:
  using TimeFunc = base::TimeTicks (*)();

  // From this direction.
  enum Direction {
    kClient = 0,
    kServer = 1,
    kNumDirections = 2,
    kNone = 2,
  };

  class Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    virtual int OnConnectServer(unsigned int connection_id,
                                const StreamSocket* accepted_socket,
                                ClientSocketHandle* server_socket,
                                CompletionRepeatingCallback callback) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  NaiveConnection(unsigned int id,
                  Direction pad_direction,
                  std::unique_ptr<StreamSocket> accepted_socket,
                  Delegate* delegate,
                  const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveConnection();

  unsigned int id() const { return id_; }
  int Connect(CompletionOnceCallback callback);
  void Disconnect();
  int Run(CompletionOnceCallback callback);

 private:
  enum State {
    STATE_CONNECT_CLIENT,
    STATE_CONNECT_CLIENT_COMPLETE,
    STATE_CONNECT_SERVER,
    STATE_CONNECT_SERVER_COMPLETE,
    STATE_NONE,
  };

  enum PaddingState {
    STATE_READ_PAYLOAD_LENGTH_1,
    STATE_READ_PAYLOAD_LENGTH_2,
    STATE_READ_PADDING_LENGTH,
    STATE_READ_PAYLOAD,
    STATE_READ_PADDING,
  };

  void DoCallback(int result);
  void OnIOComplete(int result);
  int DoLoop(int last_io_result);
  int DoConnectClient();
  int DoConnectClientComplete(int result);
  int DoConnectServer();
  int DoConnectServerComplete(int result);
  void Pull(Direction from, Direction to);
  void Push(Direction from, Direction to, int size);
  void Disconnect(Direction side);
  bool IsConnected(Direction side);
  void OnBothDisconnected();
  void OnPullError(Direction from, Direction to, int error);
  void OnPushError(Direction from, Direction to, int error);
  void OnPullComplete(Direction from, Direction to, int result);
  void OnPushComplete(Direction from, Direction to, int result);

  unsigned int id_;
  Direction pad_direction_;

  CompletionRepeatingCallback io_callback_;
  CompletionOnceCallback connect_callback_;
  CompletionOnceCallback run_callback_;

  State next_state_;

  Delegate* delegate_;

  std::unique_ptr<StreamSocket> client_socket_;
  std::unique_ptr<ClientSocketHandle> server_socket_handle_;

  StreamSocket* sockets_[kNumDirections];
  scoped_refptr<IOBuffer> read_buffers_[kNumDirections];
  scoped_refptr<DrainableIOBuffer> write_buffers_[kNumDirections];
  int errors_[kNumDirections];
  bool write_pending_[kNumDirections];
  int bytes_passed_without_yielding_[kNumDirections];
  base::TimeTicks yield_after_time_[kNumDirections];

  bool early_pull_pending_;
  bool can_push_to_server_;
  int early_pull_result_;

  int num_paddings_[kNumDirections];
  PaddingState read_padding_state_;
  int payload_length_;
  int padding_length_;

  bool full_duplex_;

  TimeFunc time_func_;

  // Traffic annotation for socket control.
  const NetworkTrafficAnnotationTag& traffic_annotation_;

  base::WeakPtrFactory<NaiveConnection> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NaiveConnection);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_CONNECTION_H_
