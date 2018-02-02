// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_WEBSOCKET_ENDPOINT_LOCK_MANAGER_H_
#define NET_SOCKET_WEBSOCKET_ENDPOINT_LOCK_MANAGER_H_

#include <stddef.h>

#include <map>

#include "base/containers/linked_list.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_export.h"
#include "net/socket/websocket_transport_client_socket_pool.h"

namespace net {

class StreamSocket;

// Keep track of ongoing WebSocket connections in order to satisfy the WebSocket
// connection throttling requirements described in RFC6455 4.1.2:
//
//   2.  If the client already has a WebSocket connection to the remote
//       host (IP address) identified by /host/ and port /port/ pair, even
//       if the remote host is known by another name, the client MUST wait
//       until that connection has been established or for that connection
//       to have failed.  There MUST be no more than one connection in a
//       CONNECTING state.  If multiple connections to the same IP address
//       are attempted simultaneously, the client MUST serialize them so
//       that there is no more than one connection at a time running
//       through the following steps.
//
// This class is neither thread-safe nor thread-compatible.
// TODO(ricea): Make this class thread-compatible by making it not be a
// singleton.
class NET_EXPORT_PRIVATE WebSocketEndpointLockManager {
 public:
  // Implement this interface to wait for an endpoint to be available.
  class NET_EXPORT_PRIVATE Waiter : public base::LinkNode<Waiter> {
   public:
    // If the node is in a list, removes it.
    virtual ~Waiter();

    virtual void GotEndpointLock() = 0;
  };

  static WebSocketEndpointLockManager* GetInstance();

  // Returns OK if lock was acquired immediately, ERR_IO_PENDING if not. If the
  // lock was not acquired, then |waiter->GotEndpointLock()| will be called when
  // it is. A Waiter automatically removes itself from the list of waiters when
  // its destructor is called.
  int LockEndpoint(const IPEndPoint& endpoint, Waiter* waiter);

  // Records the IPEndPoint associated with a particular socket. This is
  // necessary because TCPClientSocket refuses to return the PeerAddress after
  // the connection is disconnected. The association will be forgotten when
  // UnlockSocket() or UnlockEndpoint() is called. The |socket| pointer must not
  // be deleted between the call to RememberSocket() and the call to
  // UnlockSocket().
  void RememberSocket(StreamSocket* socket, const IPEndPoint& endpoint);

  // Removes the socket association that was recorded by RememberSocket(), then
  // asynchronously releases the lock on the endpoint after a delay. If
  // appropriate, calls |waiter->GetEndpointLock()| when the lock is
  // released. Should be called exactly once for each |socket| that was passed
  // to RememberSocket(). Does nothing if UnlockEndpoint() has been called since
  // the call to RememberSocket().
  void UnlockSocket(StreamSocket* socket);

  // Asynchronously releases the lock on |endpoint| after a delay. Does nothing
  // if |endpoint| is not locked.  Removes any socket association that was
  // recorded with RememberSocket(). If appropriate, calls
  // |waiter->GotEndpointLock()| when the lock is released.
  void UnlockEndpoint(const IPEndPoint& endpoint);

  // Checks that |lock_info_map_| and |socket_lock_info_map_| are empty. For
  // tests.
  bool IsEmpty() const;

  // Changes the value of the unlock delay. Returns the previous value of the
  // delay.
  base::TimeDelta SetUnlockDelayForTesting(base::TimeDelta new_delay);

 private:
  friend struct base::LazyInstanceTraitsBase<net::WebSocketEndpointLockManager>;

  struct LockInfo {
    typedef base::LinkedList<Waiter> WaiterQueue;

    LockInfo();
    ~LockInfo();

    // This object must be copyable to be placed in the map, but it cannot be
    // copied after |queue| has been assigned to.
    LockInfo(const LockInfo& rhs);

    // Not used.
    LockInfo& operator=(const LockInfo& rhs);

    // Must be NULL to copy this object into the map. Must be set to non-NULL
    // after the object is inserted into the map then point to the same list
    // until this object is deleted.
    std::unique_ptr<WaiterQueue> queue;

    // This pointer is only used to identify the last instance of StreamSocket
    // that was passed to RememberSocket() for this endpoint. It should only be
    // compared with other pointers. It is never dereferenced and not owned. It
    // is non-NULL if RememberSocket() has been called for this endpoint since
    // the last call to UnlockSocket() or UnlockEndpoint().
    StreamSocket* socket;
  };

  // SocketLockInfoMap requires std::map iterator semantics for LockInfoMap
  // (ie. that the iterator will remain valid as long as the entry is not
  // deleted).
  typedef std::map<IPEndPoint, LockInfo> LockInfoMap;
  typedef std::map<StreamSocket*, LockInfoMap::iterator> SocketLockInfoMap;

  WebSocketEndpointLockManager();
  ~WebSocketEndpointLockManager();

  void UnlockEndpointAfterDelay(const IPEndPoint& endpoint);
  void DelayedUnlockEndpoint(const IPEndPoint& endpoint);
  void EraseSocket(LockInfoMap::iterator lock_info_it);

  // If an entry is present in the map for a particular endpoint, then that
  // endpoint is locked. If LockInfo.queue is non-empty, then one or more
  // Waiters are waiting for the lock.
  LockInfoMap lock_info_map_;

  // Store sockets remembered by RememberSocket() and not yet unlocked by
  // UnlockSocket() or UnlockEndpoint(). Every entry in this map always
  // references a live entry in lock_info_map_, and the LockInfo::socket member
  // is non-NULL if and only if there is an entry in this map for the socket.
  SocketLockInfoMap socket_lock_info_map_;

  // Time to wait between a call to Unlock* and actually unlocking the socket.
  base::TimeDelta unlock_delay_;

  // Number of sockets currently pending unlock.
  size_t pending_unlock_count_;

  DISALLOW_COPY_AND_ASSIGN(WebSocketEndpointLockManager);
};

}  // namespace net

#endif  // NET_SOCKET_WEBSOCKET_ENDPOINT_LOCK_MANAGER_H_
