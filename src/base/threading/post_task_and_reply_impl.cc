// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/post_task_and_reply_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/debug/leak_annotations.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace base {

namespace {

class PostTaskAndReplyRelay {
 public:
  PostTaskAndReplyRelay(const Location& from_here,
                        OnceClosure task,
                        OnceClosure reply)
      : from_here_(from_here),
        task_(std::move(task)),
        reply_(std::move(reply)) {}
  PostTaskAndReplyRelay(PostTaskAndReplyRelay&&) = default;

  ~PostTaskAndReplyRelay() {
    if (reply_) {
      // This can run:
      // 1) On origin sequence, when:
      //    1a) Posting |task_| fails.
      //    1b) |reply_| is cancelled before running.
      //    1c) The DeleteSoon() below is scheduled.
      // 2) On destination sequence, when:
      //    2a) |task_| is cancelled before running.
      //    2b) Posting |reply_| fails.

      if (!reply_task_runner_->RunsTasksInCurrentSequence()) {
        // Case 2a) or 2b).
        //
        // Destroy callbacks asynchronously on |reply_task_runner| since their
        // destructors can rightfully be affine to it. As always, DeleteSoon()
        // might leak its argument if the target execution environment is
        // shutdown (e.g. MessageLoop deleted, TaskScheduler shutdown).
        //
        // Note: while it's obvious why |reply_| can be affine to
        // |reply_task_runner|, the reason that |task_| can also be affine to it
        // is that it if neither tasks ran, |task_| may still hold an object
        // which was intended to be moved to |reply_| when |task_| ran (such an
        // object's destruction can be affine to |reply_task_runner_| -- e.g.
        // https://crbug.com/829122).
        auto relay_to_delete =
            std::make_unique<PostTaskAndReplyRelay>(std::move(*this));
        ANNOTATE_LEAKING_OBJECT_PTR(relay_to_delete.get());
        reply_task_runner_->DeleteSoon(from_here_, std::move(relay_to_delete));
      }

      // Case 1a), 1b), 1c).
      //
      // Callbacks will be destroyed synchronously at the end of this scope.
    } else {
      // This can run when both callbacks have run or have been moved to another
      // PostTaskAndReplyRelay instance. If |reply_| is null, |task_| must be
      // null too.
      DCHECK(!task_);
    }
  }

  // No assignment operator because of const members.
  PostTaskAndReplyRelay& operator=(PostTaskAndReplyRelay&&) = delete;

  // Static function is used because it is not possible to bind a method call to
  // a non-pointer type.
  static void RunTaskAndPostReply(PostTaskAndReplyRelay relay) {
    DCHECK(relay.task_);
    std::move(relay.task_).Run();

    // Keep a reference to the reply TaskRunner for the PostTask() call before
    // |relay| is moved into a callback.
    scoped_refptr<SequencedTaskRunner> reply_task_runner =
        relay.reply_task_runner_;

    reply_task_runner->PostTask(
        relay.from_here_,
        BindOnce(&PostTaskAndReplyRelay::RunReply, std::move(relay)));
  }

 private:
  // Static function is used because it is not possible to bind a method call to
  // a non-pointer type.
  static void RunReply(PostTaskAndReplyRelay relay) {
    DCHECK(!relay.task_);
    DCHECK(relay.reply_);
    std::move(relay.reply_).Run();
  }

  const Location from_here_;
  OnceClosure task_;
  OnceClosure reply_;
  const scoped_refptr<SequencedTaskRunner> reply_task_runner_ =
      SequencedTaskRunnerHandle::Get();

  DISALLOW_COPY_AND_ASSIGN(PostTaskAndReplyRelay);
};

}  // namespace

namespace internal {

bool PostTaskAndReplyImpl::PostTaskAndReply(const Location& from_here,
                                            OnceClosure task,
                                            OnceClosure reply) {
  DCHECK(task) << from_here.ToString();
  DCHECK(reply) << from_here.ToString();

  return PostTask(from_here,
                  BindOnce(&PostTaskAndReplyRelay::RunTaskAndPostReply,
                           PostTaskAndReplyRelay(from_here, std::move(task),
                                                 std::move(reply))));
}

}  // namespace internal

}  // namespace base
