// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_descriptor_watcher_posix.h"

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_local.h"

namespace base {

namespace {

// MessageLoopForIO used to watch file descriptors for which callbacks are
// registered from a given thread.
LazyInstance<ThreadLocalPointer<MessageLoopForIO>>::Leaky
    tls_message_loop_for_io = LAZY_INSTANCE_INITIALIZER;

}  // namespace

FileDescriptorWatcher::Controller::~Controller() {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  // Delete |watcher_| on the MessageLoopForIO.
  //
  // If the MessageLoopForIO is deleted before Watcher::StartWatching() runs,
  // |watcher_| is leaked. If the MessageLoopForIO is deleted after
  // Watcher::StartWatching() runs but before the DeleteSoon task runs,
  // |watcher_| is deleted from Watcher::WillDestroyCurrentMessageLoop().
  message_loop_for_io_task_runner_->DeleteSoon(FROM_HERE, watcher_.release());

  // Since WeakPtrs are invalidated by the destructor, RunCallback() won't be
  // invoked after this returns.
}

class FileDescriptorWatcher::Controller::Watcher
    : public MessageLoopForIO::Watcher,
      public MessageLoop::DestructionObserver {
 public:
  Watcher(WeakPtr<Controller> controller, MessageLoopForIO::Mode mode, int fd);
  ~Watcher() override;

  void StartWatching();

 private:
  friend class FileDescriptorWatcher;

  // MessageLoopForIO::Watcher:
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  // MessageLoop::DestructionObserver:
  void WillDestroyCurrentMessageLoop() override;

  // Used to instruct the MessageLoopForIO to stop watching the file descriptor.
  MessageLoopForIO::FileDescriptorWatcher file_descriptor_watcher_;

  // Runs tasks on the sequence on which this was instantiated (i.e. the
  // sequence on which the callback must run).
  const scoped_refptr<SequencedTaskRunner> callback_task_runner_ =
      SequencedTaskRunnerHandle::Get();

  // The Controller that created this Watcher.
  WeakPtr<Controller> controller_;

  // Whether this Watcher is notified when |fd_| becomes readable or writable
  // without blocking.
  const MessageLoopForIO::Mode mode_;

  // The watched file descriptor.
  const int fd_;

  // Except for the constructor, every method of this class must run on the same
  // MessageLoopForIO thread.
  ThreadChecker thread_checker_;

  // Whether this Watcher was registered as a DestructionObserver on the
  // MessageLoopForIO thread.
  bool registered_as_destruction_observer_ = false;

  DISALLOW_COPY_AND_ASSIGN(Watcher);
};

FileDescriptorWatcher::Controller::Watcher::Watcher(
    WeakPtr<Controller> controller,
    MessageLoopForIO::Mode mode,
    int fd)
    : file_descriptor_watcher_(FROM_HERE),
      controller_(controller),
      mode_(mode),
      fd_(fd) {
  DCHECK(callback_task_runner_);
  thread_checker_.DetachFromThread();
}

FileDescriptorWatcher::Controller::Watcher::~Watcher() {
  DCHECK(thread_checker_.CalledOnValidThread());
  MessageLoopForIO::current()->RemoveDestructionObserver(this);
}

void FileDescriptorWatcher::Controller::Watcher::StartWatching() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!MessageLoopForIO::current()->WatchFileDescriptor(
          fd_, false, mode_, &file_descriptor_watcher_, this)) {
    // TODO(wez): Ideally we would [D]CHECK here, or propagate the failure back
    // to the caller, but there is no guarantee that they haven't already
    // closed |fd_| on another thread, so the best we can do is Debug-log.
    DLOG(ERROR) << "Failed to watch fd=" << fd_;
  }

  if (!registered_as_destruction_observer_) {
    MessageLoopForIO::current()->AddDestructionObserver(this);
    registered_as_destruction_observer_ = true;
  }
}

void FileDescriptorWatcher::Controller::Watcher::OnFileCanReadWithoutBlocking(
    int fd) {
  DCHECK_EQ(fd_, fd);
  DCHECK_EQ(MessageLoopForIO::WATCH_READ, mode_);
  DCHECK(thread_checker_.CalledOnValidThread());

  // Run the callback on the sequence on which the watch was initiated.
  callback_task_runner_->PostTask(
      FROM_HERE, BindOnce(&Controller::RunCallback, controller_));
}

void FileDescriptorWatcher::Controller::Watcher::OnFileCanWriteWithoutBlocking(
    int fd) {
  DCHECK_EQ(fd_, fd);
  DCHECK_EQ(MessageLoopForIO::WATCH_WRITE, mode_);
  DCHECK(thread_checker_.CalledOnValidThread());

  // Run the callback on the sequence on which the watch was initiated.
  callback_task_runner_->PostTask(
      FROM_HERE, BindOnce(&Controller::RunCallback, controller_));
}

void FileDescriptorWatcher::Controller::Watcher::
    WillDestroyCurrentMessageLoop() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // A Watcher is owned by a Controller. When the Controller is deleted, it
  // transfers ownership of the Watcher to a delete task posted to the
  // MessageLoopForIO. If the MessageLoopForIO is deleted before the delete task
  // runs, the following line takes care of deleting the Watcher.
  delete this;
}

FileDescriptorWatcher::Controller::Controller(MessageLoopForIO::Mode mode,
                                              int fd,
                                              const Closure& callback)
    : callback_(callback),
      message_loop_for_io_task_runner_(
          tls_message_loop_for_io.Get().Get()->task_runner()),
      weak_factory_(this) {
  DCHECK(!callback_.is_null());
  DCHECK(message_loop_for_io_task_runner_);
  watcher_ = std::make_unique<Watcher>(weak_factory_.GetWeakPtr(), mode, fd);
  StartWatching();
}

void FileDescriptorWatcher::Controller::StartWatching() {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  // It is safe to use Unretained() below because |watcher_| can only be deleted
  // by a delete task posted to |message_loop_for_io_task_runner_| by this
  // Controller's destructor. Since this delete task hasn't been posted yet, it
  // can't run before the task posted below.
  message_loop_for_io_task_runner_->PostTask(
      FROM_HERE, BindOnce(&Watcher::StartWatching, Unretained(watcher_.get())));
}

void FileDescriptorWatcher::Controller::RunCallback() {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  WeakPtr<Controller> weak_this = weak_factory_.GetWeakPtr();

  callback_.Run();

  // If |this| wasn't deleted, re-enable the watch.
  if (weak_this)
    StartWatching();
}

FileDescriptorWatcher::FileDescriptorWatcher(
    MessageLoopForIO* message_loop_for_io) {
  DCHECK(message_loop_for_io);
  DCHECK(!tls_message_loop_for_io.Get().Get());
  tls_message_loop_for_io.Get().Set(message_loop_for_io);
}

FileDescriptorWatcher::~FileDescriptorWatcher() {
  tls_message_loop_for_io.Get().Set(nullptr);
}

std::unique_ptr<FileDescriptorWatcher::Controller>
FileDescriptorWatcher::WatchReadable(int fd, const Closure& callback) {
  return WrapUnique(new Controller(MessageLoopForIO::WATCH_READ, fd, callback));
}

std::unique_ptr<FileDescriptorWatcher::Controller>
FileDescriptorWatcher::WatchWritable(int fd, const Closure& callback) {
  return WrapUnique(
      new Controller(MessageLoopForIO::WATCH_WRITE, fd, callback));
}

}  // namespace base
