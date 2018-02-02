// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A ClientSocketPoolBase is used to restrict the number of sockets open at
// a time.  It also maintains a list of idle persistent sockets for reuse.
// Subclasses of ClientSocketPool should compose ClientSocketPoolBase to handle
// the core logic of (1) restricting the number of active (connected or
// connecting) sockets per "group" (generally speaking, the hostname), (2)
// maintaining a per-group list of idle, persistent sockets for reuse, and (3)
// limiting the total number of active sockets in the system.
//
// ClientSocketPoolBase abstracts socket connection details behind ConnectJob,
// ConnectJobFactory, and SocketParams.  When a socket "slot" becomes available,
// the ClientSocketPoolBase will ask the ConnectJobFactory to create a
// ConnectJob with a SocketParams.  Subclasses of ClientSocketPool should
// implement their socket specific connection by subclassing ConnectJob and
// implementing ConnectJob::ConnectInternal().  They can control the parameters
// passed to each new ConnectJob instance via their ConnectJobFactory subclass
// and templated SocketParams parameter.
//
#ifndef NET_SOCKET_CLIENT_SOCKET_POOL_BASE_H_
#define NET_SOCKET_CLIENT_SOCKET_POOL_BASE_H_

#include <stddef.h>
#include <stdint.h>

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/base/load_states.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/base/priority_queue.h"
#include "net/base/request_priority.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/stream_socket.h"

namespace base {
class DictionaryValue;
namespace trace_event {
class ProcessMemoryDump;
}
}

namespace net {

class ClientSocketHandle;
struct NetLogSource;

// ConnectJob provides an abstract interface for "connecting" a socket.
// The connection may involve host resolution, tcp connection, ssl connection,
// etc.
class NET_EXPORT_PRIVATE ConnectJob {
 public:
  class NET_EXPORT_PRIVATE Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    // Alerts the delegate that the connection completed. |job| must
    // be destroyed by the delegate. A std::unique_ptr<> isn't used because
    // the caller of this function doesn't own |job|.
    virtual void OnConnectJobComplete(int result,
                                      ConnectJob* job) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // A |timeout_duration| of 0 corresponds to no timeout.
  ConnectJob(const std::string& group_name,
             base::TimeDelta timeout_duration,
             RequestPriority priority,
             ClientSocketPool::RespectLimits respect_limits,
             Delegate* delegate,
             const NetLogWithSource& net_log);
  virtual ~ConnectJob();

  // Accessors
  const std::string& group_name() const { return group_name_; }
  const NetLogWithSource& net_log() { return net_log_; }

  // Releases ownership of the underlying socket to the caller.
  // Returns the released socket, or NULL if there was a connection
  // error.
  std::unique_ptr<StreamSocket> PassSocket();

  void set_motivation(HttpRequestInfo::RequestMotivation motivation) {
    motivation_ = motivation;
  }

  // Begins connecting the socket.  Returns OK on success, ERR_IO_PENDING if it
  // cannot complete synchronously without blocking, or another net error code
  // on error.  In asynchronous completion, the ConnectJob will notify
  // |delegate_| via OnConnectJobComplete.  In both asynchronous and synchronous
  // completion, ReleaseSocket() can be called to acquire the connected socket
  // if it succeeded.
  int Connect();

  virtual LoadState GetLoadState() const = 0;

  // If Connect returns an error (or OnConnectJobComplete reports an error
  // result) this method will be called, allowing the pool to add
  // additional error state to the ClientSocketHandle (post late-binding).
  virtual void GetAdditionalErrorState(ClientSocketHandle* handle) {}

  const LoadTimingInfo::ConnectTiming& connect_timing() const {
    return connect_timing_;
  }

  const NetLogWithSource& net_log() const { return net_log_; }

 protected:
  RequestPriority priority() const { return priority_; }
  ClientSocketPool::RespectLimits respect_limits() const {
    return respect_limits_;
  }
  void SetSocket(std::unique_ptr<StreamSocket> socket);
  StreamSocket* socket() { return socket_.get(); }
  void NotifyDelegateOfCompletion(int rv);
  void ResetTimer(base::TimeDelta remainingTime);

  // Connection establishment timing information.
  LoadTimingInfo::ConnectTiming connect_timing_;

 private:
  virtual int ConnectInternal() = 0;

  void LogConnectStart();
  void LogConnectCompletion(int net_error);

  // Alerts the delegate that the ConnectJob has timed out.
  void OnTimeout();

  const std::string group_name_;
  const base::TimeDelta timeout_duration_;
  // TODO(akalin): Support reprioritization.
  const RequestPriority priority_;
  const ClientSocketPool::RespectLimits respect_limits_;
  // Timer to abort jobs that take too long.
  base::OneShotTimer timer_;
  Delegate* delegate_;
  std::unique_ptr<StreamSocket> socket_;
  NetLogWithSource net_log_;
  HttpRequestInfo::RequestMotivation motivation_;
  // A ConnectJob is idle until Connect() has been called.
  bool idle_;

  DISALLOW_COPY_AND_ASSIGN(ConnectJob);
};

namespace internal {

// ClientSocketPoolBaseHelper is an internal class that implements almost all
// the functionality from ClientSocketPoolBase without using templates.
// ClientSocketPoolBase adds templated definitions built on top of
// ClientSocketPoolBaseHelper.  This class is not for external use, please use
// ClientSocketPoolBase instead.
class NET_EXPORT_PRIVATE ClientSocketPoolBaseHelper
    : public ConnectJob::Delegate,
      public NetworkChangeNotifier::IPAddressObserver {
 public:
  typedef uint32_t Flags;

  // Used to specify specific behavior for the ClientSocketPool.
  enum Flag {
    NORMAL = 0,  // Normal behavior.
    NO_IDLE_SOCKETS = 0x1,  // Do not return an idle socket. Create a new one.
  };

  class NET_EXPORT_PRIVATE Request {
   public:
    Request(ClientSocketHandle* handle,
            const CompletionCallback& callback,
            RequestPriority priority,
            ClientSocketPool::RespectLimits respect_limits,
            Flags flags,
            const NetLogWithSource& net_log);

    virtual ~Request();

    ClientSocketHandle* handle() const { return handle_; }
    const CompletionCallback& callback() const { return callback_; }
    RequestPriority priority() const { return priority_; }
    void set_priority(RequestPriority priority) { priority_ = priority; }
    ClientSocketPool::RespectLimits respect_limits() const {
      return respect_limits_;
    }
    Flags flags() const { return flags_; }
    const NetLogWithSource& net_log() const { return net_log_; }

    // TODO(eroman): Temporary until crbug.com/467797 is solved.
    void CrashIfInvalid() const;

   private:
    // TODO(eroman): Temporary until crbug.com/467797 is solved.
    enum Liveness {
      ALIVE = 0xCA11AB13,
      DEAD = 0xDEADBEEF,
    };

    ClientSocketHandle* const handle_;
    const CompletionCallback callback_;
    RequestPriority priority_;
    const ClientSocketPool::RespectLimits respect_limits_;
    const Flags flags_;
    const NetLogWithSource net_log_;

    // TODO(eroman): Temporary until crbug.com/467797 is solved.
    Liveness liveness_ = ALIVE;

    DISALLOW_COPY_AND_ASSIGN(Request);
  };

  class ConnectJobFactory {
   public:
    ConnectJobFactory() {}
    virtual ~ConnectJobFactory() {}

    virtual std::unique_ptr<ConnectJob> NewConnectJob(
        const std::string& group_name,
        const Request& request,
        ConnectJob::Delegate* delegate) const = 0;

    virtual base::TimeDelta ConnectionTimeout() const = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(ConnectJobFactory);
  };

  ClientSocketPoolBaseHelper(
      HigherLayeredPool* pool,
      int max_sockets,
      int max_sockets_per_group,
      base::TimeDelta unused_idle_socket_timeout,
      base::TimeDelta used_idle_socket_timeout,
      ConnectJobFactory* connect_job_factory);

  ~ClientSocketPoolBaseHelper() override;

  // Adds a lower layered pool to |this|, and adds |this| as a higher layered
  // pool on top of |lower_pool|.
  void AddLowerLayeredPool(LowerLayeredPool* lower_pool);

  // See LowerLayeredPool::IsStalled for documentation on this function.
  bool IsStalled() const;

  // See LowerLayeredPool for documentation on these functions. It is expected
  // in the destructor that no higher layer pools remain.
  void AddHigherLayeredPool(HigherLayeredPool* higher_pool);
  void RemoveHigherLayeredPool(HigherLayeredPool* higher_pool);

  // See ClientSocketPool::RequestSocket for documentation on this function.
  int RequestSocket(const std::string& group_name,
                    std::unique_ptr<Request> request);

  // See ClientSocketPool::RequestSockets for documentation on this function.
  void RequestSockets(const std::string& group_name,
                      const Request& request,
                      int num_sockets,
                      HttpRequestInfo::RequestMotivation motivation);

  // See ClientSocketPool::SetPriority for documentation on this function.
  void SetPriority(const std::string& group_name,
                   ClientSocketHandle* handle,
                   RequestPriority priority);

  // See ClientSocketPool::CancelRequest for documentation on this function.
  void CancelRequest(const std::string& group_name,
                     ClientSocketHandle* handle);

  // See ClientSocketPool::ReleaseSocket for documentation on this function.
  void ReleaseSocket(const std::string& group_name,
                     std::unique_ptr<StreamSocket> socket,
                     int id);

  // See ClientSocketPool::FlushWithError for documentation on this function.
  void FlushWithError(int error);

  // See ClientSocketPool::CloseIdleSockets for documentation on this function.
  void CloseIdleSockets();

  // See ClientSocketPool::CloseIdleSocketsInGroup for documentation.
  void CloseIdleSocketsInGroup(const std::string& group_name);

  // See ClientSocketPool::IdleSocketCount() for documentation on this function.
  int idle_socket_count() const {
    return idle_socket_count_;
  }

  // See ClientSocketPool::IdleSocketCountInGroup() for documentation on this
  // function.
  int IdleSocketCountInGroup(const std::string& group_name) const;

  // See ClientSocketPool::GetLoadState() for documentation on this function.
  LoadState GetLoadState(const std::string& group_name,
                         const ClientSocketHandle* handle) const;

  base::TimeDelta ConnectRetryInterval() const {
    // TODO(mbelshe): Make this tuned dynamically based on measured RTT.
    //                For now, just use the max retry interval.
    return base::TimeDelta::FromMilliseconds(
        ClientSocketPool::kMaxConnectRetryIntervalMs);
  }

  int NumUnassignedConnectJobsInGroup(const std::string& group_name) const {
    return group_map_.find(group_name)->second->unassigned_job_count();
  }

  int NumConnectJobsInGroup(const std::string& group_name) const {
    return group_map_.find(group_name)->second->jobs().size();
  }

  int NumActiveSocketsInGroup(const std::string& group_name) const {
    return group_map_.find(group_name)->second->active_socket_count();
  }

  bool HasGroup(const std::string& group_name) const;

  // Closes all idle sockets if |force| is true.  Else, only closes idle
  // sockets that timed out or can't be reused.  Made public for testing.
  void CleanupIdleSockets(bool force);

  // Closes one idle socket.  Picks the first one encountered.
  // TODO(willchan): Consider a better algorithm for doing this.  Perhaps we
  // should keep an ordered list of idle sockets, and close them in order.
  // Requires maintaining more state.  It's not clear if it's worth it since
  // I'm not sure if we hit this situation often.
  bool CloseOneIdleSocket();

  // Checks higher layered pools to see if they can close an idle connection.
  bool CloseOneIdleConnectionInHigherLayeredPool();

  // See ClientSocketPool::GetInfoAsValue for documentation on this function.
  std::unique_ptr<base::DictionaryValue> GetInfoAsValue(
      const std::string& name,
      const std::string& type) const;

  // Dumps memory allocation stats. |parent_dump_absolute_name| is the name
  // used by the parent MemoryAllocatorDump in the memory dump hierarchy.
  void DumpMemoryStats(base::trace_event::ProcessMemoryDump* pmd,
                       const std::string& parent_dump_absolute_name) const;

  base::TimeDelta ConnectionTimeout() const {
    return connect_job_factory_->ConnectionTimeout();
  }

  static bool connect_backup_jobs_enabled();
  static bool set_connect_backup_jobs_enabled(bool enabled);

  void EnableConnectBackupJobs();

  // ConnectJob::Delegate methods:
  void OnConnectJobComplete(int result, ConnectJob* job) override;

  // NetworkChangeNotifier::IPAddressObserver methods:
  void OnIPAddressChanged() override;

 private:
  // Entry for a persistent socket which became idle at time |start_time|.
  struct IdleSocket {
    IdleSocket() : socket(NULL) {}

    // An idle socket can't be used if it is disconnected or has been used
    // before and has received data unexpectedly (hence no longer idle).  The
    // unread data would be mistaken for the beginning of the next response if
    // we were to use the socket for a new request.
    //
    // Note that a socket that has never been used before (like a preconnected
    // socket) may be used even with unread data.  This may be, e.g., a SPDY
    // SETTINGS frame.
    bool IsUsable() const;

    StreamSocket* socket;
    base::TimeTicks start_time;
  };

  typedef PriorityQueue<Request*> RequestQueue;
  typedef std::map<const ClientSocketHandle*, Request*> RequestMap;

  // A Group is allocated per group_name when there are idle sockets or pending
  // requests.  Otherwise, the Group object is removed from the map.
  // |active_socket_count| tracks the number of sockets held by clients.
  class Group {
   public:
    using JobList = std::list<std::unique_ptr<ConnectJob>>;

    Group();
    ~Group();

    bool IsEmpty() const {
      return active_socket_count_ == 0 && idle_sockets_.empty() &&
          jobs_.empty() && pending_requests_.empty();
    }

    bool HasAvailableSocketSlot(int max_sockets_per_group) const {
      return NumActiveSocketSlots() < max_sockets_per_group;
    }

    int NumActiveSocketSlots() const {
      return active_socket_count_ + static_cast<int>(jobs_.size()) +
          static_cast<int>(idle_sockets_.size());
    }

    // Returns true if the group could make use of an additional socket slot, if
    // it were given one.
    bool CanUseAdditionalSocketSlot(int max_sockets_per_group) const {
      return HasAvailableSocketSlot(max_sockets_per_group) &&
          pending_requests_.size() > jobs_.size();
    }

    // Returns the priority of the top of the pending request queue
    // (which may be less than the maximum priority over the entire
    // queue, due to how we prioritize requests with |respect_limits|
    // DISABLED over others).
    RequestPriority TopPendingPriority() const {
      // NOTE: FirstMax().value()->priority() is not the same as
      // FirstMax().priority()!
      return pending_requests_.FirstMax().value()->priority();
    }

    // Set a timer to create a backup job if it takes too long to
    // create one and if a timer isn't already running.
    void StartBackupJobTimer(const std::string& group_name,
                             ClientSocketPoolBaseHelper* pool);

    bool BackupJobTimerIsRunning() const;

    // If there's a ConnectJob that's never been assigned to Request,
    // decrements |unassigned_job_count_| and returns true.
    // Otherwise, returns false.
    bool TryToUseUnassignedConnectJob();

    void AddJob(std::unique_ptr<ConnectJob> job, bool is_preconnect);
    // Remove |job| from this group, which must already own |job|.
    void RemoveJob(ConnectJob* job);
    void RemoveAllJobs();

    bool has_pending_requests() const {
      return !pending_requests_.empty();
    }

    size_t pending_request_count() const {
      return pending_requests_.size();
    }

    // Gets (but does not remove) the next pending request. Returns
    // NULL if there are no pending requests.
    const Request* GetNextPendingRequest() const;

    // Returns true if there is a connect job for |handle|.
    bool HasConnectJobForHandle(const ClientSocketHandle* handle) const;

    // Inserts the request into the queue based on priority
    // order. Older requests are prioritized over requests of equal
    // priority.
    void InsertPendingRequest(std::unique_ptr<Request> request);

    // Gets and removes the next pending request. Returns NULL if
    // there are no pending requests.
    std::unique_ptr<Request> PopNextPendingRequest();

    // Finds the pending request for |handle| and removes it. Returns
    // the removed pending request, or NULL if there was none.
    std::unique_ptr<Request> FindAndRemovePendingRequest(
        ClientSocketHandle* handle);

    // Change the priority of the request named by |*handle|.  |*handle|
    // must refer to a request currently present in the group.  If |priority|
    // is the same as the current priority of the request, this is a no-op.
    void SetPriority(ClientSocketHandle* handle, RequestPriority priority);

    void IncrementActiveSocketCount() { active_socket_count_++; }
    void DecrementActiveSocketCount() { active_socket_count_--; }

    int unassigned_job_count() const { return unassigned_job_count_; }
    const JobList& jobs() const { return jobs_; }
    const std::list<IdleSocket>& idle_sockets() const { return idle_sockets_; }
    int active_socket_count() const { return active_socket_count_; }
    std::list<IdleSocket>* mutable_idle_sockets() { return &idle_sockets_; }

   private:
    // Returns the iterator's pending request after removing it from
    // the queue.
    std::unique_ptr<Request> RemovePendingRequest(
        const RequestQueue::Pointer& pointer);

    // Called when the backup socket timer fires.
    void OnBackupJobTimerFired(
        std::string group_name,
        ClientSocketPoolBaseHelper* pool);

    // Checks that |unassigned_job_count_| does not execeed the number of
    // ConnectJobs.
    void SanityCheck();

    // Total number of ConnectJobs that have never been assigned to a Request.
    // Since jobs use late binding to requests, which ConnectJobs have or have
    // not been assigned to a request are not tracked.  This is incremented on
    // preconnect and decremented when a preconnect is assigned, or when there
    // are fewer than |unassigned_job_count_| ConnectJobs.  Not incremented
    // when a request is cancelled.
    size_t unassigned_job_count_;

    std::list<IdleSocket> idle_sockets_;
    JobList jobs_;
    RequestQueue pending_requests_;
    int active_socket_count_;  // number of active sockets used by clients
    // A timer for when to start the backup job.
    base::OneShotTimer backup_job_timer_;
  };

  typedef std::map<std::string, Group*> GroupMap;

  typedef std::set<ConnectJob*> ConnectJobSet;

  struct CallbackResultPair {
    CallbackResultPair();
    CallbackResultPair(const CompletionCallback& callback_in, int result_in);
    CallbackResultPair(const CallbackResultPair& other);
    ~CallbackResultPair();

    CompletionCallback callback;
    int result;
  };

  typedef std::map<const ClientSocketHandle*, CallbackResultPair>
      PendingCallbackMap;

  // Closes all idle sockets in |group| if |force| is true.  Else, only closes
  // idle sockets in |group| that timed out with respect to |now| or can't be
  // reused.
  void CleanupIdleSocketsInGroup(bool force,
                                 Group* group,
                                 const base::TimeTicks& now);

  Group* GetOrCreateGroup(const std::string& group_name);
  void RemoveGroup(const std::string& group_name);
  void RemoveGroup(GroupMap::iterator it);

  // Called when the number of idle sockets changes.
  void IncrementIdleCount();
  void DecrementIdleCount();

  // Scans the group map for groups which have an available socket slot and
  // at least one pending request. Returns true if any groups are stalled, and
  // if so (and if both |group| and |group_name| are not NULL), fills |group|
  // and |group_name| with data of the stalled group having highest priority.
  bool FindTopStalledGroup(Group** group, std::string* group_name) const;

  // Removes |job| from |group|, which must already own |job|.
  void RemoveConnectJob(ConnectJob* job, Group* group);

  // Tries to see if we can handle any more requests for |group|.
  void OnAvailableSocketSlot(const std::string& group_name, Group* group);

  // Process a pending socket request for a group.
  void ProcessPendingRequest(const std::string& group_name, Group* group);

  // Assigns |socket| to |handle| and updates |group|'s counters appropriately.
  void HandOutSocket(std::unique_ptr<StreamSocket> socket,
                     ClientSocketHandle::SocketReuseType reuse_type,
                     const LoadTimingInfo::ConnectTiming& connect_timing,
                     ClientSocketHandle* handle,
                     base::TimeDelta time_idle,
                     Group* group,
                     const NetLogWithSource& net_log);

  // Adds |socket| to the list of idle sockets for |group|.
  void AddIdleSocket(std::unique_ptr<StreamSocket> socket, Group* group);

  // Iterates through |group_map_|, canceling all ConnectJobs and deleting
  // groups if they are no longer needed.
  void CancelAllConnectJobs();

  // Iterates through |group_map_|, posting |error| callbacks for all
  // requests, and then deleting groups if they are no longer needed.
  void CancelAllRequestsWithError(int error);

  // Returns true if we can't create any more sockets due to the total limit.
  bool ReachedMaxSocketsLimit() const;

  // This is the internal implementation of RequestSocket().  It differs in that
  // it does not handle logging into NetLog of the queueing status of
  // |request|.
  int RequestSocketInternal(const std::string& group_name,
                            const Request& request,
                            HttpRequestInfo::RequestMotivation motivation);

  // Assigns an idle socket for the group to the request.
  // Returns |true| if an idle socket is available, false otherwise.
  bool AssignIdleSocketToRequest(const Request& request, Group* group);

  static void LogBoundConnectJobToRequest(
      const NetLogSource& connect_job_source,
      const Request& request);

  // Same as CloseOneIdleSocket() except it won't close an idle socket in
  // |group|.  If |group| is NULL, it is ignored.  Returns true if it closed a
  // socket.
  bool CloseOneIdleSocketExceptInGroup(const Group* group);

  // Checks if there are stalled socket groups that should be notified
  // for possible wakeup.
  void CheckForStalledSocketGroups();

  // Posts a task to call InvokeUserCallback() on the next iteration through the
  // current message loop.  Inserts |callback| into |pending_callback_map_|,
  // keyed by |handle|.
  void InvokeUserCallbackLater(
      ClientSocketHandle* handle, const CompletionCallback& callback, int rv);

  // Invokes the user callback for |handle|.  By the time this task has run,
  // it's possible that the request has been cancelled, so |handle| may not
  // exist in |pending_callback_map_|.  We look up the callback and result code
  // in |pending_callback_map_|.
  void InvokeUserCallback(ClientSocketHandle* handle);

  // Tries to close idle sockets in a higher level socket pool as long as this
  // this pool is stalled.
  void TryToCloseSocketsInLayeredPools();

  GroupMap group_map_;

  // Map of the ClientSocketHandles for which we have a pending Task to invoke a
  // callback.  This is necessary since, before we invoke said callback, it's
  // possible that the request is cancelled.
  PendingCallbackMap pending_callback_map_;

  // The total number of idle sockets in the system.
  int idle_socket_count_;

  // Number of connecting sockets across all groups.
  int connecting_socket_count_;

  // Number of connected sockets we handed out across all groups.
  int handed_out_socket_count_;

  // The maximum total number of sockets. See ReachedMaxSocketsLimit.
  const int max_sockets_;

  // The maximum number of sockets kept per group.
  const int max_sockets_per_group_;

  // The time to wait until closing idle sockets.
  const base::TimeDelta unused_idle_socket_timeout_;
  const base::TimeDelta used_idle_socket_timeout_;

  const std::unique_ptr<ConnectJobFactory> connect_job_factory_;

  // TODO(vandebo) Remove when backup jobs move to TransportClientSocketPool
  bool connect_backup_jobs_enabled_;

  // A unique id for the pool.  It gets incremented every time we
  // FlushWithError() the pool.  This is so that when sockets get released back
  // to the pool, we can make sure that they are discarded rather than reused.
  int pool_generation_number_;

  // Used to add |this| as a higher layer pool on top of lower layer pools.  May
  // be NULL if no lower layer pools will be added.
  HigherLayeredPool* pool_;

  // Pools that create connections through |this|.  |this| will try to close
  // their idle sockets when it stalls.  Must be empty on destruction.
  std::set<HigherLayeredPool*> higher_pools_;

  // Pools that this goes through.  Typically there's only one, but not always.
  // |this| will check if they're stalled when it has a new idle socket.  |this|
  // will remove itself from all lower layered pools on destruction.
  std::set<LowerLayeredPool*> lower_pools_;

  base::WeakPtrFactory<ClientSocketPoolBaseHelper> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ClientSocketPoolBaseHelper);
};

}  // namespace internal

template <typename SocketParams>
class ClientSocketPoolBase {
 public:
  class Request : public internal::ClientSocketPoolBaseHelper::Request {
   public:
    Request(ClientSocketHandle* handle,
            const CompletionCallback& callback,
            RequestPriority priority,
            ClientSocketPool::RespectLimits respect_limits,
            internal::ClientSocketPoolBaseHelper::Flags flags,
            const scoped_refptr<SocketParams>& params,
            const NetLogWithSource& net_log)
        : internal::ClientSocketPoolBaseHelper::Request(handle,
                                                        callback,
                                                        priority,
                                                        respect_limits,
                                                        flags,
                                                        net_log),
          params_(params) {}

    const scoped_refptr<SocketParams>& params() const { return params_; }

   private:
    const scoped_refptr<SocketParams> params_;
  };

  class ConnectJobFactory {
   public:
    ConnectJobFactory() {}
    virtual ~ConnectJobFactory() {}

    virtual std::unique_ptr<ConnectJob> NewConnectJob(
        const std::string& group_name,
        const Request& request,
        ConnectJob::Delegate* delegate) const = 0;

    virtual base::TimeDelta ConnectionTimeout() const = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(ConnectJobFactory);
  };

  // |max_sockets| is the maximum number of sockets to be maintained by this
  // ClientSocketPool.  |max_sockets_per_group| specifies the maximum number of
  // sockets a "group" can have.  |unused_idle_socket_timeout| specifies how
  // long to leave an unused idle socket open before closing it.
  // |used_idle_socket_timeout| specifies how long to leave a previously used
  // idle socket open before closing it.
  ClientSocketPoolBase(HigherLayeredPool* self,
                       int max_sockets,
                       int max_sockets_per_group,
                       base::TimeDelta unused_idle_socket_timeout,
                       base::TimeDelta used_idle_socket_timeout,
                       ConnectJobFactory* connect_job_factory)
      : helper_(self,
                max_sockets,
                max_sockets_per_group,
                unused_idle_socket_timeout,
                used_idle_socket_timeout,
                new ConnectJobFactoryAdaptor(connect_job_factory)) {}

  virtual ~ClientSocketPoolBase() {}

  // These member functions simply forward to ClientSocketPoolBaseHelper.
  void AddLowerLayeredPool(LowerLayeredPool* lower_pool) {
    helper_.AddLowerLayeredPool(lower_pool);
  }

  void AddHigherLayeredPool(HigherLayeredPool* higher_pool) {
    helper_.AddHigherLayeredPool(higher_pool);
  }

  void RemoveHigherLayeredPool(HigherLayeredPool* higher_pool) {
    helper_.RemoveHigherLayeredPool(higher_pool);
  }

  // RequestSocket bundles up the parameters into a Request and then forwards to
  // ClientSocketPoolBaseHelper::RequestSocket().
  int RequestSocket(const std::string& group_name,
                    const scoped_refptr<SocketParams>& params,
                    RequestPriority priority,
                    ClientSocketPool::RespectLimits respect_limits,
                    ClientSocketHandle* handle,
                    const CompletionCallback& callback,
                    const NetLogWithSource& net_log) {
    std::unique_ptr<Request> request(new Request(
        handle, callback, priority, respect_limits,
        internal::ClientSocketPoolBaseHelper::NORMAL, params, net_log));
    return helper_.RequestSocket(group_name, std::move(request));
  }

  // RequestSockets bundles up the parameters into a Request and then forwards
  // to ClientSocketPoolBaseHelper::RequestSockets().  Note that it assigns the
  // priority to IDLE and specifies the NO_IDLE_SOCKETS flag.
  void RequestSockets(const std::string& group_name,
                      const scoped_refptr<SocketParams>& params,
                      int num_sockets,
                      const NetLogWithSource& net_log,
                      HttpRequestInfo::RequestMotivation motivation) {
    const Request request(nullptr /* no handle */, CompletionCallback(), IDLE,
                          ClientSocketPool::RespectLimits::ENABLED,
                          internal::ClientSocketPoolBaseHelper::NO_IDLE_SOCKETS,
                          params, net_log);
    helper_.RequestSockets(group_name, request, num_sockets, motivation);
  }

  void SetPriority(const std::string& group_name,
                   ClientSocketHandle* handle,
                   RequestPriority priority) {
    return helper_.SetPriority(group_name, handle, priority);
  }

  void CancelRequest(const std::string& group_name,
                     ClientSocketHandle* handle) {
    return helper_.CancelRequest(group_name, handle);
  }

  void ReleaseSocket(const std::string& group_name,
                     std::unique_ptr<StreamSocket> socket,
                     int id) {
    return helper_.ReleaseSocket(group_name, std::move(socket), id);
  }

  void FlushWithError(int error) { helper_.FlushWithError(error); }

  bool IsStalled() const { return helper_.IsStalled(); }

  void CloseIdleSockets() { return helper_.CloseIdleSockets(); }

  void CloseIdleSocketsInGroup(const std::string& group_name) {
    return helper_.CloseIdleSocketsInGroup(group_name);
  }

  int idle_socket_count() const { return helper_.idle_socket_count(); }

  int IdleSocketCountInGroup(const std::string& group_name) const {
    return helper_.IdleSocketCountInGroup(group_name);
  }

  LoadState GetLoadState(const std::string& group_name,
                         const ClientSocketHandle* handle) const {
    return helper_.GetLoadState(group_name, handle);
  }

  void DumpMemoryStats(base::trace_event::ProcessMemoryDump* pmd,
                       const std::string& parent_dump_absolute_name) const {
    return helper_.DumpMemoryStats(pmd, parent_dump_absolute_name);
  }

  virtual void OnConnectJobComplete(int result, ConnectJob* job) {
    return helper_.OnConnectJobComplete(result, job);
  }

  int NumUnassignedConnectJobsInGroup(const std::string& group_name) const {
    return helper_.NumUnassignedConnectJobsInGroup(group_name);
  }

  int NumConnectJobsInGroup(const std::string& group_name) const {
    return helper_.NumConnectJobsInGroup(group_name);
  }

  int NumActiveSocketsInGroup(const std::string& group_name) const {
    return helper_.NumActiveSocketsInGroup(group_name);
  }

  bool HasGroup(const std::string& group_name) const {
    return helper_.HasGroup(group_name);
  }

  void CleanupIdleSockets(bool force) {
    return helper_.CleanupIdleSockets(force);
  }

  std::unique_ptr<base::DictionaryValue> GetInfoAsValue(
      const std::string& name,
      const std::string& type) const {
    return helper_.GetInfoAsValue(name, type);
  }

  base::TimeDelta ConnectionTimeout() const {
    return helper_.ConnectionTimeout();
  }

  void EnableConnectBackupJobs() { helper_.EnableConnectBackupJobs(); }

  bool CloseOneIdleSocket() { return helper_.CloseOneIdleSocket(); }

  bool CloseOneIdleConnectionInHigherLayeredPool() {
    return helper_.CloseOneIdleConnectionInHigherLayeredPool();
  }

 private:
  // This adaptor class exists to bridge the
  // internal::ClientSocketPoolBaseHelper::ConnectJobFactory and
  // ClientSocketPoolBase::ConnectJobFactory types, allowing clients to use the
  // typesafe ClientSocketPoolBase::ConnectJobFactory, rather than having to
  // static_cast themselves.
  class ConnectJobFactoryAdaptor
      : public internal::ClientSocketPoolBaseHelper::ConnectJobFactory {
   public:
    typedef typename ClientSocketPoolBase<SocketParams>::ConnectJobFactory
        ConnectJobFactory;

    explicit ConnectJobFactoryAdaptor(ConnectJobFactory* connect_job_factory)
        : connect_job_factory_(connect_job_factory) {}
    ~ConnectJobFactoryAdaptor() override {}

    std::unique_ptr<ConnectJob> NewConnectJob(
        const std::string& group_name,
        const internal::ClientSocketPoolBaseHelper::Request& request,
        ConnectJob::Delegate* delegate) const override {
      const Request& casted_request = static_cast<const Request&>(request);
      return connect_job_factory_->NewConnectJob(
          group_name, casted_request, delegate);
    }

    base::TimeDelta ConnectionTimeout() const override {
      return connect_job_factory_->ConnectionTimeout();
    }

    const std::unique_ptr<ConnectJobFactory> connect_job_factory_;
  };

  internal::ClientSocketPoolBaseHelper helper_;

  DISALLOW_COPY_AND_ASSIGN(ClientSocketPoolBase);
};

}  // namespace net

#endif  // NET_SOCKET_CLIENT_SOCKET_POOL_BASE_H_
