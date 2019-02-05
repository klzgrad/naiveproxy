// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_CLIENT_SOCKET_POOL_H_
#define NET_SOCKET_CLIENT_SOCKET_POOL_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/load_states.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_request_info.h"

namespace base {
class DictionaryValue;
}

namespace net {

class ClientSocketHandle;
class NetLogWithSource;
class StreamSocket;

// ClientSocketPools are layered. This defines an interface for lower level
// socket pools to communicate with higher layer pools.
class NET_EXPORT HigherLayeredPool {
 public:
  virtual ~HigherLayeredPool() {}

  // Instructs the HigherLayeredPool to close an idle connection. Return true if
  // one was closed.  Closing an idle connection will call into the lower layer
  // pool it came from, so must be careful of re-entrancy when using this.
  virtual bool CloseOneIdleConnection() = 0;
};

// ClientSocketPools are layered. This defines an interface for higher level
// socket pools to communicate with lower layer pools.
class NET_EXPORT LowerLayeredPool {
 public:
  virtual ~LowerLayeredPool() {}

  // Returns true if a there is currently a request blocked on the per-pool
  // (not per-host) max socket limit, either in this pool, or one that it is
  // layered on top of.
  virtual bool IsStalled() const = 0;

  // Called to add or remove a higher layer pool on top of |this|.  A higher
  // layer pool may be added at most once to |this|, and must be removed prior
  // to destruction of |this|.
  virtual void AddHigherLayeredPool(HigherLayeredPool* higher_pool) = 0;
  virtual void RemoveHigherLayeredPool(HigherLayeredPool* higher_pool) = 0;
};

// A ClientSocketPool is used to restrict the number of sockets open at a time.
// It also maintains a list of idle persistent sockets.
//
// Subclasses must also have an inner class SocketParams which is
// the type for the |params| argument in RequestSocket() and
// RequestSockets() below.
class NET_EXPORT ClientSocketPool : public LowerLayeredPool {
 public:
  // Indicates whether or not a request for a socket should respect the
  // SocketPool's global and per-group socket limits.
  enum class RespectLimits { DISABLED, ENABLED };

  // Requests a connected socket for a group_name.
  //
  // There are five possible results from calling this function:
  // 1) RequestSocket returns OK and initializes |handle| with a reused socket.
  // 2) RequestSocket returns OK with a newly connected socket.
  // 3) RequestSocket returns ERR_IO_PENDING.  The handle will be added to a
  // wait list until a socket is available to reuse or a new socket finishes
  // connecting.  |priority| will determine the placement into the wait list.
  // 4) An error occurred early on, so RequestSocket returns an error code.
  // 5) A recoverable error occurred while setting up the socket.  An error
  // code is returned, but the |handle| is initialized with the new socket.
  // The caller must recover from the error before using the connection, or
  // Disconnect the socket before releasing or resetting the |handle|.
  // The current recoverable errors are: the errors accepted by
  // IsCertificateError(err) and PROXY_AUTH_REQUESTED, or
  // HTTPS_PROXY_TUNNEL_RESPONSE when reported by HttpProxyClientSocketPool.
  //
  // If this function returns OK, then |handle| is initialized upon return.
  // The |handle|'s is_initialized method will return true in this case.  If a
  // StreamSocket was reused, then ClientSocketPool will call
  // |handle|->set_reused(true).  In either case, the socket will have been
  // allocated and will be connected.  A client might want to know whether or
  // not the socket is reused in order to request a new socket if it encounters
  // an error with the reused socket.
  //
  // If ERR_IO_PENDING is returned, then the callback will be used to notify the
  // client of completion.
  //
  // Profiling information for the request is saved to |net_log| if non-NULL.
  //
  // If |respect_limits| is DISABLED, priority must be HIGHEST.
  virtual int RequestSocket(const std::string& group_name,
                            const void* params,
                            RequestPriority priority,
                            const SocketTag& socket_tag,
                            RespectLimits respect_limits,
                            ClientSocketHandle* handle,
                            CompletionOnceCallback callback,
                            const NetLogWithSource& net_log) = 0;

  // RequestSockets is used to request that |num_sockets| be connected in the
  // connection group for |group_name|.  If the connection group already has
  // |num_sockets| idle sockets / active sockets / currently connecting sockets,
  // then this function doesn't do anything.  Otherwise, it will start up as
  // many connections as necessary to reach |num_sockets| total sockets for the
  // group.  It uses |params| to control how to connect the sockets.   The
  // ClientSocketPool will assign a priority to the new connections, if any.
  // This priority will probably be lower than all others, since this method
  // is intended to make sure ahead of time that |num_sockets| sockets are
  // available to talk to a host.
  virtual void RequestSockets(const std::string& group_name,
                              const void* params,
                              int num_sockets,
                              const NetLogWithSource& net_log) = 0;

  // Called to change the priority of a RequestSocket call that returned
  // ERR_IO_PENDING and has not yet asynchronously completed.  The same handle
  // parameter must be passed to this method as was passed to the
  // RequestSocket call being modified.
  // This function is a no-op if |priority| is the same as the current
  // request priority.
  virtual void SetPriority(const std::string& group_name,
                           ClientSocketHandle* handle,
                           RequestPriority priority) = 0;

  // Called to cancel a RequestSocket call that returned ERR_IO_PENDING.  The
  // same handle parameter must be passed to this method as was passed to the
  // RequestSocket call being cancelled.  The associated callback is not run.
  // However, for performance, we will let one ConnectJob complete and go idle.
  virtual void CancelRequest(const std::string& group_name,
                             ClientSocketHandle* handle) = 0;

  // Called to release a socket once the socket is no longer needed.  If the
  // socket still has an established connection, then it will be added to the
  // set of idle sockets to be used to satisfy future RequestSocket calls.
  // Otherwise, the StreamSocket is destroyed.  |id| is used to differentiate
  // between updated versions of the same pool instance.  The pool's id will
  // change when it flushes, so it can use this |id| to discard sockets with
  // mismatched ids.
  virtual void ReleaseSocket(const std::string& group_name,
                             std::unique_ptr<StreamSocket> socket,
                             int id) = 0;

  // This flushes all state from the ClientSocketPool.  This means that all
  // idle and connecting sockets are discarded with the given |error|.
  // Active sockets being held by ClientSocketPool clients will be discarded
  // when released back to the pool.
  // Does not flush any pools wrapped by |this|.
  virtual void FlushWithError(int error) = 0;

  // Called to close any idle connections held by the connection manager.
  virtual void CloseIdleSockets() = 0;

  // Called to close any idle connections held by the connection manager.
  virtual void CloseIdleSocketsInGroup(const std::string& group_name) = 0;

  // The total number of idle sockets in the pool.
  virtual int IdleSocketCount() const = 0;

  // The total number of idle sockets in a connection group.
  virtual int IdleSocketCountInGroup(const std::string& group_name) const = 0;

  // Determine the LoadState of a connecting ClientSocketHandle.
  virtual LoadState GetLoadState(const std::string& group_name,
                                 const ClientSocketHandle* handle) const = 0;

  // Retrieves information on the current state of the pool as a
  // DictionaryValue.
  // If |include_nested_pools| is true, the states of any nested
  // ClientSocketPools will be included.
  virtual std::unique_ptr<base::DictionaryValue> GetInfoAsValue(
      const std::string& name,
      const std::string& type,
      bool include_nested_pools) const = 0;

  // Returns the maximum amount of time to wait before retrying a connect.
  static const int kMaxConnectRetryIntervalMs = 250;

  static base::TimeDelta unused_idle_socket_timeout();
  static void set_unused_idle_socket_timeout(base::TimeDelta timeout);

  static base::TimeDelta used_idle_socket_timeout();
  static void set_used_idle_socket_timeout(base::TimeDelta timeout);

 protected:
  ClientSocketPool();
  ~ClientSocketPool() override;

  // Return the connection timeout for this pool.
  virtual base::TimeDelta ConnectionTimeout() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ClientSocketPool);
};

template <typename PoolType>
void RequestSocketsForPool(
    PoolType* pool,
    const std::string& group_name,
    const scoped_refptr<typename PoolType::SocketParams>& params,
    int num_sockets,
    const NetLogWithSource& net_log) {
  pool->RequestSockets(group_name, &params, num_sockets, net_log);
}

}  // namespace net

#endif  // NET_SOCKET_CLIENT_SOCKET_POOL_H_
