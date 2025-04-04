// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump_io_ios.h"

#include "base/notreached.h"

namespace base {

MessagePumpIOSForIO::FdWatchController::FdWatchController(
    const Location& from_here)
    : FdWatchControllerInterface(from_here) {}

MessagePumpIOSForIO::FdWatchController::~FdWatchController() {
  StopWatchingFileDescriptor();
}

bool MessagePumpIOSForIO::FdWatchController::StopWatchingFileDescriptor() {
  if (fdref_ == NULL) {
    return true;
  }

  CFFileDescriptorDisableCallBacks(fdref_.get(), callback_types_);
  if (pump_) {
    pump_->RemoveRunLoopSource(fd_source_);
  }
  fd_source_.reset();
  fdref_.reset();
  callback_types_ = 0;
  pump_.reset();
  watcher_ = NULL;
  return true;
}

void MessagePumpIOSForIO::FdWatchController::Init(CFFileDescriptorRef fdref,
                                                  CFOptionFlags callback_types,
                                                  CFRunLoopSourceRef fd_source,
                                                  bool is_persistent) {
  DCHECK(fdref);
  DCHECK(!fdref_.is_valid());

  is_persistent_ = is_persistent;
  fdref_.reset(fdref);
  callback_types_ = callback_types;
  fd_source_.reset(fd_source);
}

void MessagePumpIOSForIO::FdWatchController::OnFileCanReadWithoutBlocking(
    int fd,
    MessagePumpIOSForIO* pump) {
  DCHECK(callback_types_ & kCFFileDescriptorReadCallBack);
  watcher_->OnFileCanReadWithoutBlocking(fd);
}

void MessagePumpIOSForIO::FdWatchController::OnFileCanWriteWithoutBlocking(
    int fd,
    MessagePumpIOSForIO* pump) {
  DCHECK(callback_types_ & kCFFileDescriptorWriteCallBack);
  watcher_->OnFileCanWriteWithoutBlocking(fd);
}

MessagePumpIOSForIO::MessagePumpIOSForIO() = default;

MessagePumpIOSForIO::~MessagePumpIOSForIO() = default;

bool MessagePumpIOSForIO::WatchFileDescriptor(int fd,
                                              bool persistent,
                                              int mode,
                                              FdWatchController* controller,
                                              FdWatcher* delegate) {
  DCHECK_GE(fd, 0);
  DCHECK(controller);
  DCHECK(delegate);
  DCHECK(mode == WATCH_READ || mode == WATCH_WRITE || mode == WATCH_READ_WRITE);

  // WatchFileDescriptor should be called on the pump thread. It is not
  // threadsafe, and your watcher may never be registered.
  DCHECK(watch_file_descriptor_caller_checker_.CalledOnValidThread());

  CFFileDescriptorContext source_context = {0};
  source_context.info = controller;

  CFOptionFlags callback_types = 0;
  if (mode & WATCH_READ) {
    callback_types |= kCFFileDescriptorReadCallBack;
  }
  if (mode & WATCH_WRITE) {
    callback_types |= kCFFileDescriptorWriteCallBack;
  }

  CFFileDescriptorRef fdref = controller->fdref_.get();
  if (fdref == NULL) {
    apple::ScopedCFTypeRef<CFFileDescriptorRef> scoped_fdref(
        CFFileDescriptorCreate(kCFAllocatorDefault, fd, false, HandleFdIOEvent,
                               &source_context));
    if (scoped_fdref == NULL) {
      NOTREACHED() << "CFFileDescriptorCreate failed";
    }

    CFFileDescriptorEnableCallBacks(scoped_fdref, callback_types);

    // TODO(wtc): what should the 'order' argument be?
    apple::ScopedCFTypeRef<CFRunLoopSourceRef> scoped_fd_source(
        CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, scoped_fdref,
                                            0));
    if (scoped_fd_source == NULL) {
      NOTREACHED() << "CFFileDescriptorCreateRunLoopSource failed";
    }
    CFRunLoopAddSource(run_loop(), scoped_fd_source, kCFRunLoopCommonModes);

    // Transfer ownership of scoped_fdref and fd_source to controller.
    controller->Init(scoped_fdref.release(), callback_types,
                     scoped_fd_source.release(), persistent);
  } else {
    // It's illegal to use this function to listen on 2 separate fds with the
    // same |controller|.
    if (CFFileDescriptorGetNativeDescriptor(fdref) != fd) {
      NOTREACHED() << "FDs don't match: "
                   << CFFileDescriptorGetNativeDescriptor(fdref)
                   << " != " << fd;
    }
    if (persistent != controller->is_persistent_) {
      NOTREACHED() << "persistent doesn't match";
    }

    // Combine old/new event masks.
    CFFileDescriptorDisableCallBacks(fdref, controller->callback_types_);
    controller->callback_types_ |= callback_types;
    CFFileDescriptorEnableCallBacks(fdref, controller->callback_types_);
  }

  controller->set_watcher(delegate);
  controller->set_pump(weak_factory_.GetWeakPtr());

  return true;
}

void MessagePumpIOSForIO::RemoveRunLoopSource(CFRunLoopSourceRef source) {
  CFRunLoopRemoveSource(run_loop(), source, kCFRunLoopCommonModes);
}

// static
void MessagePumpIOSForIO::HandleFdIOEvent(CFFileDescriptorRef fdref,
                                          CFOptionFlags callback_types,
                                          void* context) {
  FdWatchController* controller = static_cast<FdWatchController*>(context);
  DCHECK_EQ(fdref, controller->fdref_.get());

  // Ensure that |fdref| will remain live for the duration of this function
  // call even if |controller| is deleted or |StopWatchingFileDescriptor()| is
  // called, either of which will cause |fdref| to be released.
  apple::ScopedCFTypeRef<CFFileDescriptorRef> scoped_fdref(
      fdref, base::scoped_policy::RETAIN);

  int fd = CFFileDescriptorGetNativeDescriptor(fdref);
  MessagePumpIOSForIO* pump = controller->pump().get();
  DCHECK(pump);

  // Inform ThreadController of this native work item for tracking and tracing
  // purposes.
  Delegate::ScopedDoWorkItem scoped_do_work_item;
  if (pump->delegate()) {
    scoped_do_work_item = pump->delegate()->BeginWorkItem();
  }

  if (callback_types & kCFFileDescriptorWriteCallBack) {
    controller->OnFileCanWriteWithoutBlocking(fd, pump);
  }

  // Perform the read callback only if the file descriptor has not been
  // invalidated in the write callback. As |FdWatchController| invalidates
  // its file descriptor on destruction, the file descriptor being valid also
  // guarantees that |controller| has not been deleted.
  if (callback_types & kCFFileDescriptorReadCallBack &&
      CFFileDescriptorIsValid(fdref)) {
    DCHECK_EQ(fdref, controller->fdref_.get());
    controller->OnFileCanReadWithoutBlocking(fd, pump);
  }

  // Re-enable callbacks after the read/write if the file descriptor is still
  // valid and the controller is persistent.
  if (CFFileDescriptorIsValid(fdref) && controller->is_persistent_) {
    DCHECK_EQ(fdref, controller->fdref_.get());
    CFFileDescriptorEnableCallBacks(fdref, callback_types);
  }
}

}  // namespace base
