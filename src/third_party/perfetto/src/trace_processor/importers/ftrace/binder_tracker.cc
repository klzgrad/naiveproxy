/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/importers/ftrace/binder_tracker.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

// Binder tracker: displays slices for binder transactions and other operations.
// =============================================================================
//
// Supported events
// ----------------
// # Transactions
//
// * binder/binder_transaction
// * binder/binder_transaction_reply
//
// With these two events the tracker can display slices for binder transactions
// in the sending and receiving threads. Rarely, when transactions fail in some
// way, it's possible that the tracker doesn't have enough information to
// properly terminate slices. See "Commands" below for a solution.
//
// # Buffer allocations
//
// * binder/binder_transaction_alloc_buf
//
// This annotates the transaction slices (from the above events) with info about
// allocations. The event alone doesn't make sense without the "Transactions"
// events.
//
// # Commands
//
// * binder/binder_command
// * binder/binder_return
//
// These two events are only useful in conjunction with the "Transactions"
// events. Their presence allow the tracker to terminate slices more reliably
// when a transaction fails.
//
// # Locking
//
// * binder/binder_lock
// * binder/binder_locked
// * binder/binder_unlock
//
// Obsolete: this was removed from kernel v4.14
//
// Implementation details
// ----------------------
//
// # Basic transaction tracking:
//
// For each transaction, two threads are involved.
//
// A oneway (aka asynchronous) transaction has these events:
//
// ```
//      Thread Snd                                Thread Rcv
//         |                                         |
// binder_transaction(id, is_oneway)                 |
//                                                   |
//                                       binder_transaction_received(id)
// ```
//
// The tracker will create one instant events one each thread.
//
// A regular (aka synchronous) transaction has these events:
//
// ```
//      Thread Snd                                Thread Rcv
//         |                                         |
// binder_transaction(id)                            |
//         |                                         |
//         |                             binder_transaction_received(id)
//         |                                         |
//         |                             binder_transaction(other_id, is_reply)
//         |
// binder_transaction_received(other_id, is_reply)
// ```
//
// The tracker will create a "binder transaction" slice on Thread 1 and a
// "binder reply" slice on Thread 2.
//
// synchronous transactions can be nested: inside a "binder reply", a thread can
// make a binder transaction to another thread (just regular synchronous
// function calls).
//
// If a regular transaction fails, the kernel will not emit some events, causing
// the tracker to leave some slices open forever, while the threads are actually
// not working on the transaction anymore.
//
// ```
//      Thread Snd                                Thread Rcv
//         |                                         |
// binder_transaction(id)                            |
//         |                                         |
// ```
//
// or
//
// ```
//      Thread Snd                                Thread Rcv
//         |                                         |
// binder_transaction(id)                            |
//         |                                         |
//         |                             binder_transaction_received(id)
//         |                                         |
//         |                             binder_transaction(other_id, is_reply)
//         |
// ```
//
// In order to solve this problem (https://b.corp.google.com/issues/295124679),
// the tracker also understand commands and return commands. Binder commands are
// instructions that a userspace thread passes to the binder kernel driver (they
// all start with BC_), while binder return commands (they all start with BR_)
// are instructions that the binder kernel driver passes to the userspace
// thread.
//
// A synchronous transaction with commands and returns looks like this:
//
// ```
//      Thread Snd                                Thread Rcv
//         |                                         |
// binder_command(BC_TRANSACTION)                    |
//         |                                         |
// binder_transaction(id)                            |
//         |                                         |
//         |                             binder_transaction_received(id)
//         |                                         |
//         |                             binder_return(BR_TRANSACTION)
//         |                                         |
//         |                             binder_command(BC_REPLY)
//         |                                         |
//         |                             binder_transaction(other_id, is_reply)
//         |                                         |
//         |                             binder_return(BR_TRANSACTION_COMPLETE)
//         |                                         |
// binder_return(BR_TRANSACTION_COMPLETE)            |
//         |                                         |
// binder_transaction_received(other_id, is_reply)   |
//         |                                         |
// binder_return(BR_REPLY)
// ```
//
// For each thread, the tracker keeps a stack (since synchronous transactions
// can be nested). In case of failure, the tracker can observe special return
// commands (BR_DEAD_REPLY, BR_FROZEN_REPLY, ...): based on the state of the top
// of the stack it knows is it needs to terminate a slice.
//
// The tracking for commands and returns also tries to keep a correct stack, to
// avoid unbounded growth of the stack itself (even though it's internal only).
namespace perfetto {
namespace trace_processor {

namespace {
constexpr int kOneWay = 0x01;
constexpr int kRootObject = 0x04;
constexpr int kStatusCode = 0x08;
constexpr int kAcceptFds = 0x10;
constexpr int kNoFlags = 0;

std::string BinderFlagsToHuman(uint32_t flag) {
  std::string str;
  if (flag & kOneWay) {
    str += "this is a one-way call: async, no return; ";
  }
  if (flag & kRootObject) {
    str += "contents are the components root object; ";
  }
  if (flag & kStatusCode) {
    str += "contents are a 32-bit status code; ";
  }
  if (flag & kAcceptFds) {
    str += "allow replies with file descriptors; ";
  }
  if (flag == kNoFlags) {
    str += "No Flags Set";
  }
  return str;
}

}  // namespace

enum BinderTracker::TxnFrame::State : uint32_t {
  kSndAfterBC_TRANSACTION,
  kSndAfterTransaction,
  kSndAfterBR_TRANSACTION_COMPLETE,
  kSndAfterTransactionReceived,
  kRcvAfterTransactionReceived,
  kRcvAfterBR_TRANSACTION,
  kRcvAfterBC_REPLY,
  kRcvAfterTransaction,
};

BinderTracker::BinderTracker(TraceProcessorContext* context)
    : context_(context),
      binder_category_id_(context->storage->InternString("binder")),
      lock_waiting_id_(context->storage->InternString("binder lock waiting")),
      lock_held_id_(context->storage->InternString("binder lock held")),
      transaction_slice_id_(
          context->storage->InternString("binder transaction")),
      transaction_async_id_(
          context->storage->InternString("binder transaction async")),
      reply_id_(context->storage->InternString("binder reply")),
      async_rcv_id_(context->storage->InternString("binder async rcv")),
      transaction_id_(context->storage->InternString("transaction id")),
      dest_node_(context->storage->InternString("destination node")),
      dest_process_(context->storage->InternString("destination process")),
      dest_thread_(context->storage->InternString("destination thread")),
      dest_name_(context->storage->InternString("destination name")),
      is_reply_(context->storage->InternString("reply transaction?")),
      flags_(context->storage->InternString("flags")),
      code_(context->storage->InternString("code")),
      calling_tid_(context->storage->InternString("calling tid")),
      data_size_(context->storage->InternString("data size")),
      offsets_size_(context->storage->InternString("offsets size")) {}

BinderTracker::~BinderTracker() = default;

void BinderTracker::Transaction(int64_t ts,
                                uint32_t tid,
                                int32_t transaction_id,
                                int32_t dest_node,
                                uint32_t dest_tgid,
                                uint32_t dest_tid,
                                bool is_reply,
                                uint32_t flags,
                                StringId code) {
  UniqueTid src_utid = context_->process_tracker->GetOrCreateThread(tid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(src_utid);

  auto args_inserter = [this, transaction_id, dest_node, dest_tgid, is_reply,
                        flags, code,
                        tid](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(transaction_id_, Variadic::Integer(transaction_id));
    inserter->AddArg(dest_node_, Variadic::Integer(dest_node));
    inserter->AddArg(dest_process_, Variadic::Integer(dest_tgid));
    inserter->AddArg(is_reply_, Variadic::Boolean(is_reply));
    std::string flag_str =
        base::IntToHexString(flags) + " " + BinderFlagsToHuman(flags);
    inserter->AddArg(flags_, Variadic::String(context_->storage->InternString(
                                 base::StringView(flag_str))));
    inserter->AddArg(code_, Variadic::String(code));
    inserter->AddArg(calling_tid_, Variadic::UnsignedInteger(tid));
  };

  bool is_oneway = (flags & kOneWay) == kOneWay;
  auto insert_slice = [&]() {
    if (is_reply) {
      UniqueTid utid = context_->process_tracker->GetOrCreateThread(
          static_cast<uint32_t>(dest_tid));
      auto dest_thread_name = context_->storage->thread_table()[utid].name();
      auto dest_args_inserter = [this, dest_tid, &dest_thread_name](
                                    ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(dest_thread_, Variadic::Integer(dest_tid));
        if (dest_thread_name.has_value()) {
          inserter->AddArg(dest_name_, Variadic::String(*dest_thread_name));
        }
      };
      context_->slice_tracker->AddArgs(track_id, binder_category_id_, reply_id_,
                                       dest_args_inserter);
      return context_->slice_tracker->End(ts, track_id, kNullStringId,
                                          kNullStringId, args_inserter);
    }
    if (is_oneway) {
      return context_->slice_tracker->Scoped(ts, track_id, binder_category_id_,
                                             transaction_async_id_, 0,
                                             args_inserter);
    }
    return context_->slice_tracker->Begin(ts, track_id, binder_category_id_,
                                          transaction_slice_id_, args_inserter);
  };

  OutstandingTransaction transaction;
  transaction.is_reply = is_reply;
  transaction.is_oneway = is_oneway;
  transaction.args_inserter = args_inserter;
  transaction.send_track_id = track_id;
  transaction.send_slice_id = insert_slice();
  outstanding_transactions_[transaction_id] = std::move(transaction);
  auto* frame = GetTidTopFrame(tid);
  if (frame) {
    if (frame->state == TxnFrame::kSndAfterBC_TRANSACTION) {
      frame->state = TxnFrame::kSndAfterTransaction;
      frame->txn_info = {is_oneway, is_reply};
    } else if (frame->state == TxnFrame::kRcvAfterBC_REPLY) {
      frame->state = TxnFrame::kRcvAfterTransaction;
      frame->txn_info = {is_oneway, is_reply};
    } else if (frame->state == TxnFrame::kRcvAfterTransactionReceived) {
      // Probably command tracking is disabled. Let's remove the frame.
      PopTidFrame(tid);
    }
  }
}

void BinderTracker::TransactionReceived(int64_t ts,
                                        uint32_t pid,
                                        int32_t transaction_id) {
  auto it = outstanding_transactions_.find(transaction_id);
  if (it == outstanding_transactions_.end()) {
    // If we don't know what type of transaction it is, we don't know how to
    // insert the slice.
    // TODO(lalitm): maybe we should insert a dummy slice anyway - seems like
    // a questionable idea to just ignore these completely.
    return;
  }
  OutstandingTransaction transaction(std::move(it->second));
  outstanding_transactions_.erase(it);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);

  // If it's a oneway transaction, there's no stack to track on the receiving
  // side.
  if (!transaction.is_oneway) {
    if (!transaction.is_reply) {
      TxnFrame* frame = PushTidFrame(pid);
      frame->state = TxnFrame::kRcvAfterTransactionReceived;
      frame->txn_info.emplace();
      frame->txn_info->is_oneway = transaction.is_oneway;
      frame->txn_info->is_reply = transaction.is_reply;
    } else {
      TxnFrame* frame = GetTidTopFrame(pid);
      if (frame && frame->state == TxnFrame::kSndAfterBR_TRANSACTION_COMPLETE) {
        frame->state = TxnFrame::kSndAfterTransactionReceived;
      }
    }
  }

  if (transaction.is_reply) {
    // Simply end the slice started back when the first |expects_reply|
    // transaction was sent.
    context_->slice_tracker->End(ts, track_id);
    return;
  }

  std::optional<SliceId> recv_slice_id;
  if (transaction.is_oneway) {
    recv_slice_id = context_->slice_tracker->Scoped(
        ts, track_id, binder_category_id_, async_rcv_id_, 0,
        std::move(transaction.args_inserter));
  } else {
    if (transaction.send_track_id) {
      auto args_inserter = [this, utid,
                            pid](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(dest_thread_, Variadic::UnsignedInteger(pid));
        auto dest_thread_name = context_->storage->thread_table()[utid].name();
        if (dest_thread_name.has_value()) {
          inserter->AddArg(dest_name_, Variadic::String(*dest_thread_name));
        }
      };
      context_->slice_tracker->AddArgs(*transaction.send_track_id,
                                       binder_category_id_,
                                       transaction_slice_id_, args_inserter);
    }
    recv_slice_id = context_->slice_tracker->Begin(
        ts, track_id, binder_category_id_, reply_id_);
  }

  // Create a flow between the sending slice and this slice.
  if (transaction.send_slice_id && recv_slice_id) {
    context_->flow_tracker->InsertFlow(*transaction.send_slice_id,
                                       *recv_slice_id);
  }
}

void BinderTracker::CommandToKernel(int64_t /*ts*/,
                                    uint32_t tid,
                                    uint32_t cmd) {
  switch (cmd) {
    case kBC_TRANSACTION:
    case kBC_TRANSACTION_SG: {
      TxnFrame* frame = PushTidFrame(tid);
      frame->state = TxnFrame::kSndAfterBC_TRANSACTION;
      break;
    }
    case kBC_REPLY:
    case kBC_REPLY_SG: {
      TxnFrame* frame = GetTidTopFrame(tid);
      if (frame && frame->state == TxnFrame::kRcvAfterBR_TRANSACTION) {
        frame->state = TxnFrame::kRcvAfterBC_REPLY;
      }
      break;
    }
    default:
      break;
  }
}

void BinderTracker::ReturnFromKernel(int64_t ts, uint32_t tid, uint32_t cmd) {
  switch (cmd) {
    case kBR_DEAD_REPLY:
    case kBR_FAILED_REPLY:
    case kBR_FROZEN_REPLY:
    case kBR_TRANSACTION_PENDING_FROZEN: {
      TxnFrame* frame = GetTidTopFrame(tid);
      if (frame) {
        switch (frame->state) {
          case TxnFrame::kSndAfterBC_TRANSACTION:
            // The transaction has failed before we received the
            // binder_transaction event, therefore no slice has been opened.
            PopTidFrame(tid);
            break;
          case TxnFrame::kRcvAfterBC_REPLY:
          case TxnFrame::kSndAfterTransaction:
          case TxnFrame::kRcvAfterTransaction:
          case TxnFrame::kSndAfterBR_TRANSACTION_COMPLETE:
            if (frame->txn_info.has_value()) {
              if (!frame->txn_info->is_oneway && !frame->txn_info->is_reply) {
                UniqueTid utid =
                    context_->process_tracker->GetOrCreateThread(tid);
                TrackId track_id =
                    context_->track_tracker->InternThreadTrack(utid);
                context_->slice_tracker->End(ts, track_id);
              }
            }
            PopTidFrame(tid);
            break;
          case TxnFrame::kSndAfterTransactionReceived:
          case TxnFrame::kRcvAfterTransactionReceived:
          case TxnFrame::kRcvAfterBR_TRANSACTION:
            break;
        }
      }
      break;
    }

    case kBR_TRANSACTION_COMPLETE:
    case kBR_ONEWAY_SPAM_SUSPECT: {
      TxnFrame* frame = GetTidTopFrame(tid);
      if (frame) {
        if (frame->state == TxnFrame::kRcvAfterTransaction) {
          PopTidFrame(tid);
        } else if (frame->state == TxnFrame::kSndAfterBC_TRANSACTION) {
          // The transaction has failed before we received the
          // binder_transaction event, therefore no slice has been opened.
          // It's possible that the binder_transaction event was not enabled.
          PopTidFrame(tid);
        } else if (frame->state == TxnFrame::kSndAfterTransaction) {
          if (frame->txn_info.has_value() && !frame->txn_info->is_oneway) {
            frame->state = TxnFrame::kSndAfterBR_TRANSACTION_COMPLETE;
          } else {
            // For a oneway transaction, this is the last event. In any case, no
            // slice has been opened.
            PopTidFrame(tid);
          }
        }
      }
      break;
    }

    case kBR_REPLY: {
      TxnFrame* frame = GetTidTopFrame(tid);
      if (frame && frame->state == TxnFrame::kSndAfterTransactionReceived) {
        // For a synchronous transaction, this is the last event.
        PopTidFrame(tid);
      }
      break;
    }

    case kBR_TRANSACTION:
    case kBR_TRANSACTION_SEC_CTX: {
      TxnFrame* frame = GetTidTopFrame(tid);
      if (frame) {
        if (frame->state == TxnFrame::kRcvAfterTransactionReceived) {
          frame->state = TxnFrame::kRcvAfterBR_TRANSACTION;
        }
      }
      break;
    }

    default:
      break;
  }
}

void BinderTracker::Lock(int64_t ts, uint32_t pid) {
  attempt_lock_[pid] = ts;

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->Begin(ts, track_id, binder_category_id_,
                                 lock_waiting_id_);
}

void BinderTracker::Locked(int64_t ts, uint32_t pid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  if (!attempt_lock_.Find(pid))
    return;

  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(ts, track_id);
  context_->slice_tracker->Begin(ts, track_id, binder_category_id_,
                                 lock_held_id_);

  lock_acquired_[pid] = ts;
  attempt_lock_.Erase(pid);
}

void BinderTracker::Unlock(int64_t ts, uint32_t pid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  if (!lock_acquired_.Find(pid))
    return;

  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(ts, track_id, binder_category_id_,
                               lock_held_id_);
  lock_acquired_.Erase(pid);
}

void BinderTracker::TransactionAllocBuf(int64_t ts,
                                        uint32_t pid,
                                        uint64_t data_size,
                                        uint64_t offsets_size) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);

  auto args_inserter = [this, &data_size,
                        offsets_size](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(data_size_, Variadic::UnsignedInteger(data_size));
    inserter->AddArg(offsets_size_, Variadic::UnsignedInteger(offsets_size));
  };
  context_->slice_tracker->AddArgs(track_id, binder_category_id_,
                                   transaction_slice_id_, args_inserter);

  base::ignore_result(ts);
}

BinderTracker::TxnFrame* BinderTracker::GetTidTopFrame(uint32_t tid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  std::stack<BinderTracker::TxnFrame>* stack = utid_stacks_.Find(utid);
  if (stack == nullptr || stack->empty()) {
    return nullptr;
  }
  return &stack->top();
}

BinderTracker::TxnFrame* BinderTracker::PushTidFrame(uint32_t tid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  auto& stack = utid_stacks_[utid];
  stack.push({});
  return &stack.top();
}

void BinderTracker::PopTidFrame(uint32_t tid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  std::stack<BinderTracker::TxnFrame>* stack = utid_stacks_.Find(utid);
  PERFETTO_CHECK(stack);
  stack->pop();
  if (stack->empty()) {
    utid_stacks_.Erase(utid);
  }
}

}  // namespace trace_processor
}  // namespace perfetto
