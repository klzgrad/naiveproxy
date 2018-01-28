// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_cache_writers.h"

#include <algorithm>
#include <utility>

#include "base/logging.h"

#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache_transaction.h"

namespace net {

HttpCache::Writers::Writers(disk_cache::Entry* disk_entry)
    : disk_entry_(disk_entry), weak_factory_(this) {}

HttpCache::Writers::~Writers() {}

int HttpCache::Writers::Read(scoped_refptr<IOBuffer> buf,
                             int buf_len,
                             const CompletionCallback& callback,
                             Transaction* transaction) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);
  DCHECK(!callback.is_null());
  DCHECK(transaction);

  // If another transaction invoked a Read which is currently ongoing, then
  // this transaction waits for the read to complete and gets its buffer filled
  // with the data returned from that read.
  if (next_state_ != State::NONE) {
    WaitingForRead waiting_transaction(transaction, buf, buf_len, callback);
    waiting_for_read_.push_back(waiting_transaction);
    return ERR_IO_PENDING;
  }

  DCHECK_EQ(next_state_, State::NONE);
  DCHECK(callback_.is_null());
  DCHECK_EQ(nullptr, active_transaction_);
  DCHECK(HasTransaction(transaction));
  active_transaction_ = transaction;

  read_buf_ = std::move(buf);
  io_buf_len_ = buf_len;
  next_state_ = State::NETWORK_READ;

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    callback_ = callback;

  return rv;
}

bool HttpCache::Writers::StopCaching(Transaction* transaction) {
  // If this is the only transaction in Writers, then stopping will be
  // successful. If not, then we will not stop caching since there are
  // other consumers waiting to read from the cache.
  if (all_writers_.size() == 1) {
    DCHECK(all_writers_.count(transaction));
    network_read_only_ = true;
    return true;
  }
  return false;
}

void HttpCache::Writers::AddTransaction(
    Transaction* transaction,
    std::unique_ptr<HttpTransaction> network_transaction,
    bool is_exclusive) {
  DCHECK(transaction);
  DCHECK(CanAddWriters());
  DCHECK(network_transaction_ || network_transaction);

  std::pair<TransactionSet::iterator, bool> return_val =
      all_writers_.insert(transaction);
  DCHECK_EQ(return_val.second, true);

  if (is_exclusive) {
    DCHECK_EQ(1u, all_writers_.size());
    is_exclusive_ = true;
  }

  if (network_transaction) {
    DCHECK(!network_transaction_);
    network_transaction_ = std::move(network_transaction);
  }

  priority_ = std::max(transaction->priority(), priority_);
  network_transaction_->SetPriority(priority_);
}

void HttpCache::Writers::RemoveTransaction(Transaction* transaction) {
  if (!transaction)
    return;

  // The transaction should be part of all_writers.
  auto it = all_writers_.find(transaction);
  DCHECK(it != all_writers_.end());
  all_writers_.erase(it);

  if (all_writers_.empty() && next_state_ == State::NONE)
    ResetStateForEmptyWriters();
  else
    UpdatePriority();

  if (active_transaction_ == transaction) {
    active_transaction_ = nullptr;
    callback_.Reset();
    return;
  }

  auto waiting_it = waiting_for_read_.begin();
  for (; waiting_it != waiting_for_read_.end(); waiting_it++) {
    if (transaction == waiting_it->transaction) {
      waiting_for_read_.erase(waiting_it);
      // If a waiting transaction existed, there should have been an
      // active_transaction_.
      DCHECK(active_transaction_);
      return;
    }
  }
}

void HttpCache::Writers::UpdatePriority() {
  // Get the current highest priority.
  RequestPriority current_highest = MINIMUM_PRIORITY;
  for (auto* transaction : all_writers_)
    current_highest = std::max(transaction->priority(), current_highest);

  if (priority_ != current_highest) {
    network_transaction_->SetPriority(current_highest);
    priority_ = current_highest;
  }
}

bool HttpCache::Writers::ContainsOnlyIdleWriters() const {
  return waiting_for_read_.empty() && !active_transaction_;
}

HttpCache::TransactionSet HttpCache::Writers::RemoveAllIdleWriters() {
  // Should be invoked after |waiting_for_read_| transactions and
  // |active_transaction_| are processed so that all_writers_ only contains idle
  // writers.
  DCHECK(ContainsOnlyIdleWriters());

  TransactionSet idle_writers;
  idle_writers.insert(all_writers_.begin(), all_writers_.end());
  all_writers_.clear();
  ResetStateForEmptyWriters();
  return idle_writers;
}

bool HttpCache::Writers::CanAddWriters() {
  if (all_writers_.empty())
    return true;

  return !is_exclusive_ && !network_read_only_;
}

void HttpCache::Writers::ProcessFailure(Transaction* transaction, int error) {
  DCHECK(!transaction || transaction == active_transaction_);

  // Notify waiting_for_read_ of the failure. Tasks will be posted for all the
  // transactions.
  ProcessWaitingForReadTransactions(error);

  // Idle readers should fail when Read is invoked on them.
  SetIdleWritersFailState(error);

  if (all_writers_.empty())
    ResetStateForEmptyWriters();
}

void HttpCache::Writers::TruncateEntry() {
  // TODO(shivanisha) On integration, see if the entry really needs to be
  // truncated on the lines of Transaction::AddTruncatedFlag and then proceed.
  DCHECK_EQ(next_state_, State::NONE);
  next_state_ = State::CACHE_WRITE_TRUNCATED_RESPONSE;
  DoLoop(OK);
}

LoadState HttpCache::Writers::GetWriterLoadState() {
  DCHECK(network_transaction_);
  return network_transaction_->GetLoadState();
}

HttpCache::Writers::WaitingForRead::WaitingForRead(
    Transaction* cache_transaction,
    scoped_refptr<IOBuffer> buf,
    int len,
    const CompletionCallback& consumer_callback)
    : transaction(cache_transaction),
      read_buf(std::move(buf)),
      read_buf_len(len),
      write_len(0),
      callback(consumer_callback) {
  DCHECK(cache_transaction);
  DCHECK(read_buf);
  DCHECK_GT(len, 0);
  DCHECK(!consumer_callback.is_null());
}

HttpCache::Writers::WaitingForRead::~WaitingForRead() {}
HttpCache::Writers::WaitingForRead::WaitingForRead(const WaitingForRead&) =
    default;

int HttpCache::Writers::DoLoop(int result) {
  DCHECK_NE(State::UNSET, next_state_);
  DCHECK_NE(State::NONE, next_state_);
  int rv = result;
  do {
    State state = next_state_;
    next_state_ = State::UNSET;
    switch (state) {
      case State::NETWORK_READ:
        DCHECK_EQ(OK, rv);
        rv = DoNetworkRead();
        break;
      case State::NETWORK_READ_COMPLETE:
        rv = DoNetworkReadComplete(rv);
        break;
      case State::CACHE_WRITE_DATA:
        rv = DoCacheWriteData(rv);
        break;
      case State::CACHE_WRITE_DATA_COMPLETE:
        rv = DoCacheWriteDataComplete(rv);
        break;
      case State::CACHE_WRITE_TRUNCATED_RESPONSE:
        rv = DoCacheWriteTruncatedResponse();
        break;
      case State::CACHE_WRITE_TRUNCATED_RESPONSE_COMPLETE:
        rv = DoCacheWriteTruncatedResponseComplete(rv);
        break;
      case State::UNSET:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
      case State::NONE:
        // Do Nothing.
        break;
    }
  } while (next_state_ != State::NONE && rv != ERR_IO_PENDING);

  if (rv != ERR_IO_PENDING && !callback_.is_null()) {
    read_buf_ = NULL;
    base::ResetAndReturn(&callback_).Run(rv);
  }
  return rv;
}

int HttpCache::Writers::DoNetworkRead() {
  next_state_ = State::NETWORK_READ_COMPLETE;
  CompletionCallback io_callback =
      base::Bind(&HttpCache::Writers::OnIOComplete, weak_factory_.GetWeakPtr());
  return network_transaction_->Read(read_buf_.get(), io_buf_len_, io_callback);
}

int HttpCache::Writers::DoNetworkReadComplete(int result) {
  if (result < 0) {
    next_state_ = State::NONE;
    OnNetworkReadFailure(result);
    return result;
  }

  next_state_ = State::CACHE_WRITE_DATA;
  return result;
}

void HttpCache::Writers::OnNetworkReadFailure(int result) {
  ProcessFailure(active_transaction_, result);

  active_transaction_ = nullptr;

  // TODO(shivanisha): Invoke DoneWithEntry here while
  // integrating this class with HttpCache. That will also invoke truncation of
  // the entry.
}

int HttpCache::Writers::DoCacheWriteData(int num_bytes) {
  next_state_ = State::CACHE_WRITE_DATA_COMPLETE;
  write_len_ = num_bytes;
  if (!num_bytes || network_read_only_)
    return num_bytes;

  int current_size = disk_entry_->GetDataSize(kResponseContentIndex);
  CompletionCallback io_callback =
      base::Bind(&HttpCache::Writers::OnIOComplete, weak_factory_.GetWeakPtr());

  int rv = 0;

  PartialData* partial = nullptr;
  // The active transaction must be alive if this is a partial request, as
  // partial requests are exclusive and hence will always be the active
  // transaction.
  // TODO(shivanisha): When partial requests support parallel writing, this
  // assumption will not be true.
  if (active_transaction_)
    partial = active_transaction_->partial();

  if (!partial) {
    rv = disk_entry_->WriteData(kResponseContentIndex, current_size,
                                read_buf_.get(), num_bytes, io_callback, true);
  } else {
    rv = partial->CacheWrite(disk_entry_, read_buf_.get(), num_bytes,
                             io_callback);
  }
  return rv;
}

int HttpCache::Writers::DoCacheWriteDataComplete(int result) {
  if (result != write_len_) {
    OnCacheWriteFailure();

    // |active_transaction_| can continue reading from the network.
    result = write_len_;
  } else {
    OnDataReceived(result);
  }
  next_state_ = State::NONE;
  return result;
}

int HttpCache::Writers::DoCacheWriteTruncatedResponse() {
  next_state_ = State::CACHE_WRITE_TRUNCATED_RESPONSE_COMPLETE;
  const HttpResponseInfo* response = network_transaction_->GetResponseInfo();
  scoped_refptr<PickledIOBuffer> data(new PickledIOBuffer());
  response->Persist(data->pickle(), true /* skip_transient_headers*/, true);
  data->Done();
  io_buf_len_ = data->pickle()->size();
  CompletionCallback io_callback =
      base::Bind(&HttpCache::Writers::OnIOComplete, weak_factory_.GetWeakPtr());
  return disk_entry_->WriteData(kResponseInfoIndex, 0, data.get(), io_buf_len_,
                                io_callback, true);
}

int HttpCache::Writers::DoCacheWriteTruncatedResponseComplete(int result) {
  next_state_ = State::NONE;
  if (result != io_buf_len_) {
    DLOG(ERROR) << "failed to write response info to cache";

    // TODO(shivanisha): Invoke DoneWritingToEntry so that this entry is doomed.
  }
  truncated_ = true;
  return OK;
}

void HttpCache::Writers::OnDataReceived(int result) {
  if (result == 0) {
    // Check if the response is actually completed or if not, attempt to mark
    // the entry as truncated in OnNetworkReadFailure.
    int current_size = disk_entry_->GetDataSize(kResponseContentIndex);
    const HttpResponseInfo* response_info =
        network_transaction_->GetResponseInfo();
    int64_t content_length = response_info->headers->GetContentLength();
    if (content_length >= 0 && content_length > current_size) {
      OnNetworkReadFailure(result);
      return;
    }
    // TODO(shivanisha) Invoke cache_->DoneWritingToEntry() with success after
    // integration with HttpCache layer.
  }

  // Notify waiting_for_read_. Tasks will be posted for all the
  // transactions.
  ProcessWaitingForReadTransactions(write_len_);

  active_transaction_ = nullptr;

  if (all_writers_.empty())
    ResetStateForEmptyWriters();
}

void HttpCache::Writers::OnCacheWriteFailure() {
  DLOG(ERROR) << "failed to write response data to cache";

  // Now writers will only be reading from the network.
  network_read_only_ = true;

  ProcessFailure(active_transaction_, ERR_CACHE_WRITE_FAILURE);

  active_transaction_ = nullptr;

  // Call the cache_ function here even if |active_transaction_| is alive
  // because it wouldn't know if this was an error case, since it gets a
  // positive result back.
  // TODO(shivanisha) : Invoke DoneWritingToEntry on integration. Since the
  // active_transaction_ continues to read from the network, invoke
  // DoneWritingToEntry with nullptr as transaction so that it is not removed
  // from |this|.
}

void HttpCache::Writers::ProcessWaitingForReadTransactions(int result) {
  for (auto& waiting : waiting_for_read_) {
    Transaction* transaction = waiting.transaction;
    int callback_result = result;

    if (result >= 0) {  // success
      // Save the data in the waiting transaction's read buffer.
      waiting.write_len = std::min(waiting.read_buf_len, result);
      memcpy(waiting.read_buf->data(), read_buf_->data(), waiting.write_len);
      callback_result = waiting.write_len;
    }

    // If its response completion or failure, this transaction needs to be
    // removed.
    if (result <= 0)
      all_writers_.erase(transaction);

    // Post task to notify transaction.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(waiting.callback, callback_result));
  }

  waiting_for_read_.clear();
}

void HttpCache::Writers::SetIdleWritersFailState(int result) {
  // Since this is only for idle transactions, waiting_for_read_
  // should be empty.
  DCHECK(waiting_for_read_.empty());
  for (auto* transaction : all_writers_) {
    if (transaction == active_transaction_)
      continue;
    transaction->SetSharedWritingFailState(result);
    all_writers_.erase(transaction);
  }
}

void HttpCache::Writers::ResetStateForEmptyWriters() {
  DCHECK(all_writers_.empty());
  network_read_only_ = false;
  network_transaction_.reset();
}

void HttpCache::Writers::OnIOComplete(int result) {
  DoLoop(result);
}

}  // namespace net
