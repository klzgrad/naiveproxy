// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_UDP_SOCKET_POSIX_H_
#define NET_SOCKET_UDP_SOCKET_POSIX_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/threading/thread_checker.h"
#include "base/timer/timer.h"
#include "net/base/address_family.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/base/rand_callback.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/datagram_socket.h"
#include "net/socket/diff_serv_code_point.h"
#include "net/socket/socket_descriptor.h"

namespace net {

class IPAddress;
class NetLog;
struct NetLogSource;

class NET_EXPORT UDPSocketPosix {
 public:
  // Performance helper for NetworkActivityMonitor, it batches
  // throughput samples, subject to a byte limit threshold (64 KB) or
  // timer (100 ms), whichever comes first.  The batching is subject
  // to a minimum number of samples (2) required by NQE to update its
  // throughput estimate.
  class ActivityMonitor {
   public:
    ActivityMonitor() : bytes_(0), increments_(0) {}
    virtual ~ActivityMonitor() {}
    // Provided by sent/received subclass.
    // Update throughput, but batch to limit overhead of NetworkActivityMonitor.
    void Increment(uint32_t bytes);
    // For flushing cached values.
    void OnClose();

   private:
    virtual void NetworkActivityMonitorIncrement(uint32_t bytes) = 0;
    void Update();
    void OnTimerFired();

    uint32_t bytes_;
    uint32_t increments_;
    base::RepeatingTimer timer_;
    DISALLOW_COPY_AND_ASSIGN(ActivityMonitor);
  };

  class SentActivityMonitor : public ActivityMonitor {
   public:
    ~SentActivityMonitor() override {}

   private:
    void NetworkActivityMonitorIncrement(uint32_t bytes) override;
  };

  class ReceivedActivityMonitor : public ActivityMonitor {
   public:
    ~ReceivedActivityMonitor() override {}

   private:
    void NetworkActivityMonitorIncrement(uint32_t bytes) override;
  };

  UDPSocketPosix(DatagramSocket::BindType bind_type,
                 const RandIntCallback& rand_int_cb,
                 net::NetLog* net_log,
                 const net::NetLogSource& source);
  virtual ~UDPSocketPosix();

  // Opens the socket.
  // Returns a net error code.
  int Open(AddressFamily address_family);

  // Binds this socket to |network|. All data traffic on the socket will be sent
  // and received via |network|. Must be called before Connect(). This call will
  // fail if |network| has disconnected. Communication using this socket will
  // fail if |network| disconnects.
  // Returns a net error code.
  int BindToNetwork(NetworkChangeNotifier::NetworkHandle network);

  // Connects the socket to connect with a certain |address|.
  // Should be called after Open().
  // Returns a net error code.
  int Connect(const IPEndPoint& address);

  // Binds the address/port for this socket to |address|.  This is generally
  // only used on a server. Should be called after Open().
  // Returns a net error code.
  int Bind(const IPEndPoint& address);

  // Closes the socket.
  // TODO(rvargas, hidehiko): Disallow re-Open() after Close().
  void Close();

  // Copies the remote udp address into |address| and returns a net error code.
  int GetPeerAddress(IPEndPoint* address) const;

  // Copies the local udp address into |address| and returns a net error code.
  // (similar to getsockname)
  int GetLocalAddress(IPEndPoint* address) const;

  // IO:
  // Multiple outstanding read requests are not supported.
  // Full duplex mode (reading and writing at the same time) is supported

  // Reads from the socket.
  // Only usable from the client-side of a UDP socket, after the socket
  // has been connected.
  int Read(IOBuffer* buf, int buf_len, const CompletionCallback& callback);

  // Writes to the socket.
  // Only usable from the client-side of a UDP socket, after the socket
  // has been connected.
  int Write(IOBuffer* buf, int buf_len, const CompletionCallback& callback);

  // Reads from a socket and receive sender address information.
  // |buf| is the buffer to read data into.
  // |buf_len| is the maximum amount of data to read.
  // |address| is a buffer provided by the caller for receiving the sender
  //   address information about the received data.  This buffer must be kept
  //   alive by the caller until the callback is placed.
  // |callback| is the callback on completion of the RecvFrom.
  // Returns a net error code, or ERR_IO_PENDING if the IO is in progress.
  // If ERR_IO_PENDING is returned, the caller must keep |buf| and |address|
  // alive until the callback is called.
  int RecvFrom(IOBuffer* buf,
               int buf_len,
               IPEndPoint* address,
               const CompletionCallback& callback);

  // Sends to a socket with a particular destination.
  // |buf| is the buffer to send.
  // |buf_len| is the number of bytes to send.
  // |address| is the recipient address.
  // |callback| is the user callback function to call on complete.
  // Returns a net error code, or ERR_IO_PENDING if the IO is in progress.
  // If ERR_IO_PENDING is returned, the caller must keep |buf| and |address|
  // alive until the callback is called.
  int SendTo(IOBuffer* buf,
             int buf_len,
             const IPEndPoint& address,
             const CompletionCallback& callback);

  // Sets the receive buffer size (in bytes) for the socket.
  // Returns a net error code.
  int SetReceiveBufferSize(int32_t size);

  // Sets the send buffer size (in bytes) for the socket.
  // Returns a net error code.
  int SetSendBufferSize(int32_t size);

  // Requests that packets sent by this socket not be fragment, either locally
  // by the host, or by routers (via the DF bit in the IPv4 packet header).
  // May not be supported by all platforms. Returns a return a network error
  // code if there was a problem, but the socket will still be usable. Can not
  // return ERR_IO_PENDING.
  int SetDoNotFragment();

  // Returns true if the socket is already connected or bound.
  bool is_connected() const { return is_connected_; }

  const NetLogWithSource& NetLog() const { return net_log_; }

  // Call this to enable SO_REUSEADDR on the underlying socket.
  // Should be called between Open() and Bind().
  // Returns a net error code.
  int AllowAddressReuse();

  // Call this to allow or disallow sending and receiving packets to and from
  // broadcast addresses.
  // Returns a net error code.
  int SetBroadcast(bool broadcast);

  // Joins the multicast group.
  // |group_address| is the group address to join, could be either
  // an IPv4 or IPv6 address.
  // Returns a net error code.
  int JoinGroup(const IPAddress& group_address) const;

  // Leaves the multicast group.
  // |group_address| is the group address to leave, could be either
  // an IPv4 or IPv6 address. If the socket hasn't joined the group,
  // it will be ignored.
  // It's optional to leave the multicast group before destroying
  // the socket. It will be done by the OS.
  // Returns a net error code.
  int LeaveGroup(const IPAddress& group_address) const;

  // Sets interface to use for multicast. If |interface_index| set to 0,
  // default interface is used.
  // Should be called before Bind().
  // Returns a net error code.
  int SetMulticastInterface(uint32_t interface_index);

  // Sets the time-to-live option for UDP packets sent to the multicast
  // group address. The default value of this option is 1.
  // Cannot be negative or more than 255.
  // Should be called before Bind().
  // Returns a net error code.
  int SetMulticastTimeToLive(int time_to_live);

  // Sets the loopback flag for UDP socket. If this flag is true, the host
  // will receive packets sent to the joined group from itself.
  // The default value of this option is true.
  // Should be called before Bind().
  // Returns a net error code.
  //
  // Note: the behavior of |SetMulticastLoopbackMode| is slightly
  // different between Windows and Unix-like systems. The inconsistency only
  // happens when there are more than one applications on the same host
  // joined to the same multicast group while having different settings on
  // multicast loopback mode. On Windows, the applications with loopback off
  // will not RECEIVE the loopback packets; while on Unix-like systems, the
  // applications with loopback off will not SEND the loopback packets to
  // other applications on the same host. See MSDN: http://goo.gl/6vqbj
  int SetMulticastLoopbackMode(bool loopback);

  // Sets the differentiated services flags on outgoing packets. May not
  // do anything on some platforms.
  // Returns a net error code.
  int SetDiffServCodePoint(DiffServCodePoint dscp);

  // Resets the thread to be used for thread-safety checks.
  void DetachFromThread();

 private:
  enum SocketOptions {
    SOCKET_OPTION_MULTICAST_LOOP = 1 << 0
  };

  class ReadWatcher : public base::MessageLoopForIO::Watcher {
   public:
    explicit ReadWatcher(UDPSocketPosix* socket) : socket_(socket) {}

    // MessageLoopForIO::Watcher methods

    void OnFileCanReadWithoutBlocking(int /* fd */) override;

    void OnFileCanWriteWithoutBlocking(int /* fd */) override {}

   private:
    UDPSocketPosix* const socket_;

    DISALLOW_COPY_AND_ASSIGN(ReadWatcher);
  };

  class WriteWatcher : public base::MessageLoopForIO::Watcher {
   public:
    explicit WriteWatcher(UDPSocketPosix* socket) : socket_(socket) {}

    // MessageLoopForIO::Watcher methods

    void OnFileCanReadWithoutBlocking(int /* fd */) override {}

    void OnFileCanWriteWithoutBlocking(int /* fd */) override;

   private:
    UDPSocketPosix* const socket_;

    DISALLOW_COPY_AND_ASSIGN(WriteWatcher);
  };

  void DoReadCallback(int rv);
  void DoWriteCallback(int rv);
  void DidCompleteRead();
  void DidCompleteWrite();

  // Handles stats and logging. |result| is the number of bytes transferred, on
  // success, or the net error code on failure. On success, LogRead takes in a
  // sockaddr and its length, which are mandatory, while LogWrite takes in an
  // optional IPEndPoint.
  void LogRead(int result,
               const char* bytes,
               socklen_t addr_len,
               const sockaddr* addr);
  void LogWrite(int result, const char* bytes, const IPEndPoint* address);

  // Same as SendTo(), except that address is passed by pointer
  // instead of by reference. It is called from Write() with |address|
  // set to NULL.
  int SendToOrWrite(IOBuffer* buf,
                    int buf_len,
                    const IPEndPoint* address,
                    const CompletionCallback& callback);

  int InternalConnect(const IPEndPoint& address);
  int InternalRecvFrom(IOBuffer* buf, int buf_len, IPEndPoint* address);
  int InternalSendTo(IOBuffer* buf, int buf_len, const IPEndPoint* address);

  // Applies |socket_options_| to |socket_|. Should be called before
  // Bind().
  int SetMulticastOptions();
  int DoBind(const IPEndPoint& address);
  // Binds to a random port on |address|.
  int RandomBind(const IPAddress& address);

  int socket_;

  int addr_family_;
  bool is_connected_;

  // Bitwise-or'd combination of SocketOptions. Specifies the set of
  // options that should be applied to |socket_| before Bind().
  int socket_options_;

  // Multicast interface.
  uint32_t multicast_interface_;

  // Multicast socket options cached for SetMulticastOption.
  // Cannot be used after Bind().
  int multicast_time_to_live_;

  // How to do source port binding, used only when UDPSocket is part of
  // UDPClientSocket, since UDPServerSocket provides Bind.
  DatagramSocket::BindType bind_type_;

  // PRNG function for generating port numbers.
  RandIntCallback rand_int_cb_;

  // These are mutable since they're just cached copies to make
  // GetPeerAddress/GetLocalAddress smarter.
  mutable std::unique_ptr<IPEndPoint> local_address_;
  mutable std::unique_ptr<IPEndPoint> remote_address_;

  // The socket's posix wrappers
  base::MessageLoopForIO::FileDescriptorWatcher read_socket_watcher_;
  base::MessageLoopForIO::FileDescriptorWatcher write_socket_watcher_;

  // The corresponding watchers for reads and writes.
  ReadWatcher read_watcher_;
  WriteWatcher write_watcher_;

  // The buffer used by InternalRead() to retry Read requests
  scoped_refptr<IOBuffer> read_buf_;
  int read_buf_len_;
  IPEndPoint* recv_from_address_;

  // The buffer used by InternalWrite() to retry Write requests
  scoped_refptr<IOBuffer> write_buf_;
  int write_buf_len_;
  std::unique_ptr<IPEndPoint> send_to_address_;

  // External callback; called when read is complete.
  CompletionCallback read_callback_;

  // External callback; called when write is complete.
  CompletionCallback write_callback_;

  NetLogWithSource net_log_;

  // Network that this socket is bound to via BindToNetwork().
  NetworkChangeNotifier::NetworkHandle bound_network_;

  // These are used to lower the overhead updating activity monitor.
  SentActivityMonitor sent_activity_monitor_;
  ReceivedActivityMonitor received_activity_monitor_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(UDPSocketPosix);
};

}  // namespace net

#endif  // NET_SOCKET_UDP_SOCKET_POSIX_H_
