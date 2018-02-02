// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_CACHE_WRITERS_H_
#define NET_HTTP_HTTP_CACHE_WRITERS_H_

#include <list>
#include <memory>

#include "base/memory/weak_ptr.h"
#include "net/base/completion_callback.h"
#include "net/http/http_cache.h"

namespace net {

class HttpResponseInfo;
class PartialData;

// If multiple HttpCache::Transactions are accessing the same cache entry
// simultaneously, their access to the data read from network is synchronized
// by HttpCache::Writers. This enables each of those transactions to drive
// reading the response body from the network ensuring a slow consumer does not
// starve other consumers of the same resource.
//
// Writers represents the set of all HttpCache::Transactions that are
// reading from the network using the same network transaction and writing to
// the same cache entry. It is owned by the ActiveEntry.
class NET_EXPORT_PRIVATE HttpCache::Writers {
 public:
  // This is the information maintained by Writers in the context of each
  // transaction.
  // |partial| is owned by the transaction and to be sure there are no
  // dangling pointers, it must be ensured that transaction's reference and
  // this information will be removed from writers once the transaction is
  // deleted.
  struct NET_EXPORT_PRIVATE TransactionInfo {
    TransactionInfo(PartialData* partial,
                    bool truncated,
                    HttpResponseInfo info);
    ~TransactionInfo();
    TransactionInfo& operator=(const TransactionInfo&);
    TransactionInfo(const TransactionInfo&);

    PartialData* partial;
    bool truncated;
    HttpResponseInfo response_info;
  };

  // |cache| and |entry| must outlive this object.
  Writers(HttpCache* cache, HttpCache::ActiveEntry* entry);
  ~Writers();

  // Retrieves data from the network transaction associated with the Writers
  // object. This may be done directly (via a network read into |*buf->data()|)
  // or indirectly (by copying from another transactions buffer into
  // |*buf->data()| on network read completion) depending on whether or not a
  // read is currently in progress. May return the result synchronously or
  // return ERR_IO_PENDING: if ERR_IO_PENDING is returned, |callback| will be
  // run to inform the consumer of the result of the Read().
  // |transaction| may be removed while Read() is ongoing. In that case Writers
  // will still complete the Read() processing but will not invoke the
  // |callback|.
  int Read(scoped_refptr<IOBuffer> buf,
           int buf_len,
           const CompletionCallback& callback,
           Transaction* transaction);

  // Invoked when StopCaching is called on a member transaction.
  // It stops caching only if there are no other transactions. Returns true if
  // caching can be stopped.
  // |keep_entry| should be true if the entry needs to be preserved after
  // truncation.
  bool StopCaching(bool keep_entry);

  // Membership functions like AddTransaction and RemoveTransaction are invoked
  // by HttpCache on behalf of the HttpCache::Transaction.

  // Adds an HttpCache::Transaction to Writers.
  // Should only be invoked if CanAddWriters() returns true.
  // If |is_exclusive| is true, it makes writing an exclusive operation
  // implying that Writers can contain at most one transaction till the
  // completion of the response body. It is illegal to invoke with is_exclusive
  // as true if there is already a transaction present.
  // |transaction| can be destroyed at any point and it should invoke
  // HttpCache::DoneWithEntry() during its destruction. This will also ensure
  // any pointers in |info| are not accessed after the transaction is destroyed.
  void AddTransaction(Transaction* transaction,
                      bool is_exclusive,
                      RequestPriority priority,
                      const TransactionInfo& info);

  // Invoked when the transaction is done working with the entry.
  void RemoveTransaction(Transaction* transaction, bool success);

  // Invoked when there is a change in a member transaction's priority or a
  // member transaction is removed.
  void UpdatePriority();

  // Returns true if this object is empty.
  bool IsEmpty() const { return all_writers_.empty(); }

  // Invoked during HttpCache's destruction.
  void Clear() { all_writers_.clear(); }

  // Returns true if |transaction| is part of writers.
  bool HasTransaction(const Transaction* transaction) const {
    return all_writers_.count(const_cast<Transaction*>(transaction)) > 0;
  }

  // Returns true if more writers can be added for shared writing.
  bool CanAddWriters();

  // Returns if only one transaction can be a member of writers.
  bool IsExclusive() const { return is_exclusive_; }

  // Returns the network transaction which may be nullptr for range requests.
  const HttpTransaction* network_transaction() const {
    return network_transaction_.get();
  }

  // Returns the load state of the |network_transaction_| if present else
  // returns LOAD_STATE_IDLE.
  LoadState GetLoadState() const;

  // Sets the network transaction argument to |network_transaction_|. Must be
  // invoked before Read can be invoked.
  void SetNetworkTransaction(
      Transaction* transaction,
      std::unique_ptr<HttpTransaction> network_transaction);

  // Resets the network transaction to nullptr. Required for range requests as
  // they might use the current network transaction only for part of the
  // request. Must only be invoked for range requests.
  void ResetNetworkTransaction();

  // Returns if response is only being read from the network.
  bool network_read_only() const { return network_read_only_; }

  int GetTransactionsCount() const { return all_writers_.size(); }

 private:
  friend class WritersTest;

  enum class State {
    UNSET,
    NONE,
    NETWORK_READ,
    NETWORK_READ_COMPLETE,
    CACHE_WRITE_DATA,
    CACHE_WRITE_DATA_COMPLETE,
    // This state is used when an attempt to truncate races with an ongoing
    // network read/cache write. In this case we transition from
    // NETWORK_READ_COMPLETE or CACHE_WRITE_DATA_COMPLETE to
    // ASYNC_OP_COMPLETE_PRE_TRUNCATE. We will then process the truncation
    // after the ongoing operation completes.
    ASYNC_OP_COMPLETE_PRE_TRUNCATE,
    CACHE_WRITE_TRUNCATED_RESPONSE,
    CACHE_WRITE_TRUNCATED_RESPONSE_COMPLETE,
  };

  // These transactions are waiting on Read. After the active transaction
  // completes writing the data to the cache, their buffer would be filled with
  // the data and their callback will be invoked.
  struct WaitingForRead {
    scoped_refptr<IOBuffer> read_buf;
    int read_buf_len;
    int write_len;
    const CompletionCallback callback;
    WaitingForRead(scoped_refptr<IOBuffer> read_buf,
                   int len,
                   const CompletionCallback& consumer_callback);
    ~WaitingForRead();
    WaitingForRead(const WaitingForRead&);
  };
  using WaitingForReadMap = std::unordered_map<Transaction*, WaitingForRead>;

  using TransactionMap = std::unordered_map<Transaction*, TransactionInfo>;

  // Runs the state transition loop. Resets and calls |callback_| on exit,
  // unless the return value is ERR_IO_PENDING.
  int DoLoop(int result);

  // State machine functions.
  int DoNetworkRead();
  int DoNetworkReadComplete(int result);
  int DoCacheWriteData(int num_bytes);
  int DoCacheWriteDataComplete(int result);
  int DoAsyncOpCompletePreTruncate(int result);
  int DoCacheWriteTruncatedResponse();
  int DoCacheWriteTruncatedResponseComplete(int result);

  // Helper functions for callback.
  void OnNetworkReadFailure(int result);
  void OnCacheWriteFailure();
  void OnDataReceived(int result);

  // Notifies the transactions waiting on Read of the result, by posting a task
  // for each of them.
  void ProcessWaitingForReadTransactions(int result);

  // Sets the state to FAIL_READ so that any subsequent Read on an idle
  // transaction fails.
  void SetIdleWritersFailState(int result);

  // Invoked when |active_transaction_| fails to read from network or write to
  // cache. |error| indicates network read error code or cache write error.
  void ProcessFailure(int error);

  // Returns true if |this| only contains idle writers. Idle writers are those
  // that are waiting for Read to be invoked by the consumer.
  bool ContainsOnlyIdleWriters() const;

  // Returns true if its worth marking the entry as truncated.
  // TODO(shivanisha): Refactor this so that it could be const.
  bool ShouldTruncate();

  // Invoked for truncating the entry either internally within DoLoop or through
  // the API RemoveTransaction.
  // Sets |should_keep_entry_| as true if either the entry is marked as
  // truncated or the entry is already completely written and false if the entry
  // cannot be resumed later so no need to mark truncated.
  // Returns true if the function initiated truncation of the entry by changing
  // the next state to transition to CACHE_WRITE_TRUNCATED_RESPONSE or
  // ASYNC_OP_COMPLETE_PRE_TRUNCATE.
  // If a network read/cache write operation is still going on, this will not
  // initiate truncation but it will be done after the ongoing operation is
  // complete.
  // Note that if this function returns a true, its possible that |this| may
  // have been deleted and thus no members should be touched after calling this
  // function.
  bool InitiateTruncateEntry();

  // Remove the transaction.
  void EraseTransaction(Transaction* transaction, int result);
  TransactionMap::iterator EraseTransaction(TransactionMap::iterator it,
                                            int result);
  void SetCacheCallback(bool success, const TransactionSet& make_readers);

  // IO Completion callback function.
  void OnIOComplete(int result);

  State next_state_ = State::NONE;

  // True if only reading from network and not writing to cache.
  bool network_read_only_ = false;

  HttpCache* cache_ = nullptr;

  // Owner of |this|.
  ActiveEntry* entry_ = nullptr;

  std::unique_ptr<HttpTransaction> network_transaction_ = nullptr;

  scoped_refptr<IOBuffer> read_buf_ = nullptr;

  int io_buf_len_ = 0;
  int write_len_ = 0;

  // The cache transaction that is the current consumer of network_transaction_
  // ::Read or writing to the entry and is waiting for the operation to be
  // completed. This is used to ensure there is at most one consumer of
  // network_transaction_ at a time.
  Transaction* active_transaction_ = nullptr;

  // Transactions whose consumers have invoked Read, but another transaction is
  // currently the |active_transaction_|. After the network read and cache write
  // is complete, the waiting transactions will be notified.
  WaitingForReadMap waiting_for_read_;

  // Includes all transactions. ResetStateForEmptyWriters should be invoked
  // whenever all_writers_ becomes empty.
  TransactionMap all_writers_;

  // True if multiple transactions are not allowed e.g. for partial requests.
  bool is_exclusive_ = false;

  // Current priority of the request. It is always the maximum of all the writer
  // transactions.
  RequestPriority priority_ = MINIMUM_PRIORITY;

  // Response info of the most recent transaction added to Writers will be used
  // to write back the headers along with the truncated bit set. This is done so
  // that we don't overwrite headers written by a more recent transaction with
  // older headers while truncating.
  HttpResponseInfo response_info_truncation_;

  // Do not mark a partial request as truncated if it is not already a truncated
  // entry to start with.
  bool partial_do_not_truncate_ = false;

  // True if the entry should be kept, even if the response was not completely
  // written.
  bool should_keep_entry_ = true;

  // If truncation happens after a current network read/cache write then the
  // result returned to the consumer should be the one returned by the network
  // read/cache write operation.
  int post_truncate_result_ = OK;

  // True if currently processing the DoLoop.
  bool in_do_loop_ = false;

  CompletionCallback callback_;  // Callback for active_transaction_.

  // Since cache_ can destroy |this|, |cache_callback_| is only invoked at the
  // end of DoLoop().
  base::OnceClosure cache_callback_;  // Callback for cache_.

  base::WeakPtrFactory<Writers> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(Writers);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_CACHE_WRITERS_H_
