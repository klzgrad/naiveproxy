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
  // |entry| must outlive this object.
  Writers(disk_cache::Entry* entry);
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
  // TODO(shivanisha@) Also document this conditional stopping in
  // HttpTransaction on integration.
  bool StopCaching(Transaction* transaction);

  // Adds an HttpCache::Transaction to Writers and if it's the first transaction
  // added, transfers the ownership of the network transaction to Writers.
  // Should only be invoked if CanAddWriters() returns true.
  // |network_transaction| should be non-null only for the first transaction
  // and it will be assigned to |network_transaction_|. If |is_exclusive| is
  // true, it makes writing an exclusive operation implying that Writers can
  // contain at most one transaction till the completion of the response body.
  // |transaction| can be destroyed at any point and it should invoke
  // RemoveTransaction() during its destruction.
  void AddTransaction(Transaction* transaction,
                      std::unique_ptr<HttpTransaction> network_transaction,
                      bool is_exclusive);

  // Removes a transaction. Should be invoked when this transaction is
  // destroyed.
  void RemoveTransaction(Transaction* transaction);

  // Invoked when there is a change in a member transaction's priority or a
  // member transaction is removed.
  void UpdatePriority();

  // Returns true if this object is empty.
  bool IsEmpty() const { return all_writers_.empty(); }

  // Returns true if |transaction| is part of writers.
  bool HasTransaction(Transaction* transaction) const {
    return all_writers_.count(transaction) > 0;
  }

  // Remove and return any idle writers. Should only be invoked when a
  // response is completely written and when ContainesOnlyIdleWriters()
  // returns true.
  TransactionSet RemoveAllIdleWriters();

  // Returns true if more writers can be added for shared writing.
  bool CanAddWriters();

  // TODO(shivanisha), Check if this function gets invoked in the integration
  // CL. Remove if not.
  HttpTransaction* network_transaction() { return network_transaction_.get(); }

  // Invoked to mark an entry as truncated. This must only be invoked when there
  // is no ongoing Read() call.
  void TruncateEntry();

  // Should be invoked only when writers has transactions attached to it and
  // thus has a valid network transaction.
  LoadState GetWriterLoadState();

  // For testing.
  int CountTransactionsForTesting() const { return all_writers_.size(); }
  bool IsTruncatedForTesting() const { return truncated_; }

 private:
  friend class WritersTest;

  enum class State {
    UNSET,
    NONE,
    NETWORK_READ,
    NETWORK_READ_COMPLETE,
    CACHE_WRITE_DATA,
    CACHE_WRITE_DATA_COMPLETE,
    CACHE_WRITE_TRUNCATED_RESPONSE,
    CACHE_WRITE_TRUNCATED_RESPONSE_COMPLETE,
  };

  // These transactions are waiting on Read. After the active transaction
  // completes writing the data to the cache, their buffer would be filled with
  // the data and their callback will be invoked.
  struct WaitingForRead {
    Transaction* transaction;
    scoped_refptr<IOBuffer> read_buf;
    int read_buf_len;
    int write_len;
    const CompletionCallback callback;
    WaitingForRead(Transaction* transaction,
                   scoped_refptr<IOBuffer> read_buf,
                   int len,
                   const CompletionCallback& consumer_callback);
    ~WaitingForRead();
    WaitingForRead(const WaitingForRead&);
  };
  using WaitingForReadList = std::list<WaitingForRead>;

  // Runs the state transition loop. Resets and calls |callback_| on exit,
  // unless the return value is ERR_IO_PENDING.
  int DoLoop(int result);

  // State machine functions.
  int DoNetworkRead();
  int DoNetworkReadComplete(int result);
  int DoCacheWriteData(int num_bytes);
  int DoCacheWriteDataComplete(int result);
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

  // Called to reset state when all transaction references are removed from
  // |this|.
  void ResetStateForEmptyWriters();

  // Invoked when |active_transaction_| fails to read from network or write to
  // cache. |error| indicates network read error code or cache write error.
  void ProcessFailure(Transaction* transaction, int error);

  // Returns true if |this| only contains idle writers. Idle writers are those
  // that are waiting for Read to be invoked by the consumer.
  bool ContainsOnlyIdleWriters() const;

  // IO Completion callback function.
  void OnIOComplete(int result);

  State next_state_ = State::NONE;

  // True if only reading from network and not writing to cache.
  bool network_read_only_ = false;

  // TODO(shivanisha) Add HttpCache* cache_ = nullptr; on integration.

  disk_cache::Entry* disk_entry_ = nullptr;

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
  WaitingForReadList waiting_for_read_;

  // Includes all transactions. ResetStateForEmptyWriters should be invoked
  // whenever all_writers_ becomes empty.
  TransactionSet all_writers_;

  // True if multiple transactions are not allowed e.g. for partial requests.
  bool is_exclusive_ = false;

  // Current priority of the request. It is always the maximum of all the writer
  // transactions.
  RequestPriority priority_ = MINIMUM_PRIORITY;

  bool truncated_ = false;  // used for testing.

  CompletionCallback callback_;  // Callback for active_transaction_.

  base::WeakPtrFactory<Writers> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(Writers);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_CACHE_WRITERS_H_
