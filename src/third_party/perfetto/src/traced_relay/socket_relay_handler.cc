/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/traced_relay/socket_relay_handler.h"

#include <fcntl.h>
#include <sys/poll.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/platform_handle.h"
#include "perfetto/ext/base/thread_checker.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/watchdog.h"

namespace perfetto {
namespace {
// Use the default watchdog timeout for task runners.
static constexpr int kWatchdogTimeoutMs = 30000;
// Timeout of the epoll_wait() call.
static constexpr int kPollTimeoutMs = 30000;
}  // namespace

FdPoller::Watcher::~Watcher() = default;

FdPoller::FdPoller(Watcher* watcher) : watcher_(watcher) {
  WatchForRead(notify_fd_.fd());

  // This is done last in the ctor because WatchForRead() asserts using
  // |thread_checker_|.
  PERFETTO_DETACH_FROM_THREAD(thread_checker_);
}

void FdPoller::Poll() {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  int num_fds = PERFETTO_EINTR(poll(
      &poll_fds_[0], static_cast<nfds_t>(poll_fds_.size()), kPollTimeoutMs));
  if (num_fds == -1 && base::IsAgain(errno))
    return;  // Poll again.
  PERFETTO_DCHECK(num_fds <= static_cast<int>(poll_fds_.size()));

  // Make a copy of |poll_fds_| so it's safe to watch and unwatch while
  // notifying the watcher.
  const auto poll_fds(poll_fds_);

  for (const auto& event : poll_fds) {
    if (!event.revents)  // This event isn't active.
      continue;

    // Check whether the poller needs to break the polling loop for updates.
    if (event.fd == notify_fd_.fd()) {
      notify_fd_.Clear();
      continue;
    }

    // Notify the callers on fd events.
    if (event.revents & POLLOUT) {
      watcher_->OnFdWritable(event.fd);
    } else if (event.revents & POLLIN) {
      watcher_->OnFdReadable(event.fd);
    } else {
      PERFETTO_DLOG("poll() returns events %d on fd %d", event.events,
                    event.fd);
    }  // Other events like POLLHUP or POLLERR are ignored.
  }
}

void FdPoller::Notify() {
  // Can be called from any thread.
  notify_fd_.Notify();
}

std::vector<pollfd>::iterator FdPoller::FindPollEvent(base::PlatformHandle fd) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  return std::find_if(poll_fds_.begin(), poll_fds_.end(),
                      [fd](const pollfd& item) { return fd == item.fd; });
}

void FdPoller::WatchFd(base::PlatformHandle fd, WatchEvents events) {
  auto it = FindPollEvent(fd);
  if (it == poll_fds_.end()) {
    poll_fds_.push_back({fd, events, 0});
  } else {
    it->events |= events;
  }
}

void FdPoller::UnwatchFd(base::PlatformHandle fd, WatchEvents events) {
  auto it = FindPollEvent(fd);
  PERFETTO_CHECK(it != poll_fds_.end());
  it->events &= ~events;
}

void FdPoller::RemoveWatch(base::PlatformHandle fd) {
  auto it = FindPollEvent(fd);
  PERFETTO_CHECK(it != poll_fds_.end());
  poll_fds_.erase(it);
}

SocketRelayHandler::SocketRelayHandler() : fd_poller_(this) {
  PERFETTO_DETACH_FROM_THREAD(io_thread_checker_);

  io_thread_ = std::thread([this]() { this->Run(); });
}

SocketRelayHandler::~SocketRelayHandler() {
  RunOnIOThread([this]() { this->exited_ = true; });
  io_thread_.join();
}

void SocketRelayHandler::AddSocketPair(
    std::unique_ptr<SocketPair> socket_pair) {
  RunOnIOThread([this, socket_pair = std::move(socket_pair)]() mutable {
    PERFETTO_DCHECK_THREAD(io_thread_checker_);

    base::PlatformHandle fd1 = socket_pair->first.sock.fd();
    base::PlatformHandle fd2 = socket_pair->second.sock.fd();
    auto* ptr = socket_pair.get();
    socket_pairs_.emplace_back(std::move(socket_pair));

    fd_poller_.WatchForRead(fd1);
    fd_poller_.WatchForRead(fd2);

    socket_pairs_by_fd_[fd1] = ptr;
    socket_pairs_by_fd_[fd2] = ptr;
  });
}

void SocketRelayHandler::Run() {
  PERFETTO_DCHECK_THREAD(io_thread_checker_);

  while (!exited_) {
    fd_poller_.Poll();

    auto handle = base::Watchdog::GetInstance()->CreateFatalTimer(
        kWatchdogTimeoutMs, base::WatchdogCrashReason::kTaskRunnerHung);

    std::deque<std::packaged_task<void()>> pending_tasks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_tasks = std::move(pending_tasks_);
    }
    while (!pending_tasks.empty()) {
      auto task = std::move(pending_tasks.front());
      pending_tasks.pop_front();
      task();
    }
  }
}

void SocketRelayHandler::OnFdReadable(base::PlatformHandle fd) {
  PERFETTO_DCHECK_THREAD(io_thread_checker_);

  auto socket_pair = GetSocketPair(fd);
  if (!socket_pair)
    return;  // Already removed.

  auto [fd_sock, peer_sock] = *socket_pair;
  // Buffer some bytes.
  auto peer_fd = peer_sock.sock.fd();
  while (fd_sock.available_bytes() > 0) {
    auto rsize =
        fd_sock.sock.Receive(fd_sock.buffer(), fd_sock.available_bytes());
    if (rsize > 0) {
      fd_sock.EnqueueData(static_cast<size_t>(rsize));
      continue;
    }

    if (rsize == 0 || (rsize == -1 && !base::IsAgain(errno))) {
      // TODO(chinglinyu): flush the remaining data to |peer_sock|.
      RemoveSocketPair(fd_sock, peer_sock);
      return;
    }

    // If there is any buffered data that needs to be sent to |peer_sock|, arm
    // the write watcher.
    if (fd_sock.data_size() > 0) {
      fd_poller_.WatchForWrite(peer_fd);
    }
    return;
  }
  // We are not bufferable: need to turn off POLLIN to avoid spinning.
  fd_poller_.UnwatchForRead(fd);
  PERFETTO_DCHECK(fd_sock.data_size() > 0);
  // Watching for POLLOUT will cause an OnFdWritable() event of
  // |peer_sock|.
  fd_poller_.WatchForWrite(peer_fd);
}

void SocketRelayHandler::OnFdWritable(base::PlatformHandle fd) {
  PERFETTO_DCHECK_THREAD(io_thread_checker_);

  auto socket_pair = GetSocketPair(fd);
  if (!socket_pair)
    return;  // Already removed.

  auto [fd_sock, peer_sock] = *socket_pair;
  // |fd_sock| can be written to without blocking. Now we can transfer from the
  // buffer in |peer_sock|.
  while (peer_sock.data_size() > 0) {
    auto wsize = fd_sock.sock.Send(peer_sock.data(), peer_sock.data_size());
    if (wsize > 0) {
      peer_sock.DequeueData(static_cast<size_t>(wsize));
      continue;
    }

    if (wsize == -1 && !base::IsAgain(errno)) {
      RemoveSocketPair(fd_sock, peer_sock);
    }
    // errno == EAGAIN and we still have data to send: continue watching for
    // read.
    return;
  }

  // We don't have buffered data to send. Disable watching for write.
  fd_poller_.UnwatchForWrite(fd);
  auto peer_fd = peer_sock.sock.fd();
  if (peer_sock.available_bytes())
    fd_poller_.WatchForRead(peer_fd);
}

std::optional<std::tuple<SocketWithBuffer&, SocketWithBuffer&>>
SocketRelayHandler::GetSocketPair(base::PlatformHandle fd) {
  PERFETTO_DCHECK_THREAD(io_thread_checker_);

  auto* socket_pair = socket_pairs_by_fd_.Find(fd);
  if (!socket_pair)
    return std::nullopt;

  PERFETTO_DCHECK(fd == (*socket_pair)->first.sock.fd() ||
                  fd == (*socket_pair)->second.sock.fd());

  if (fd == (*socket_pair)->first.sock.fd())
    return std::tie((*socket_pair)->first, (*socket_pair)->second);

  return std::tie((*socket_pair)->second, (*socket_pair)->first);
}

void SocketRelayHandler::RemoveSocketPair(SocketWithBuffer& sock1,
                                          SocketWithBuffer& sock2) {
  PERFETTO_DCHECK_THREAD(io_thread_checker_);

  auto fd1 = sock1.sock.fd();
  auto fd2 = sock2.sock.fd();
  fd_poller_.RemoveWatch(fd1);
  fd_poller_.RemoveWatch(fd2);

  auto* ptr1 = socket_pairs_by_fd_.Find(fd1);
  auto* ptr2 = socket_pairs_by_fd_.Find(fd2);
  PERFETTO_DCHECK(ptr1 && ptr2);
  PERFETTO_DCHECK(*ptr1 == *ptr2);

  auto* socket_pair_ptr = *ptr1;

  socket_pairs_by_fd_.Erase(fd1);
  socket_pairs_by_fd_.Erase(fd2);

  socket_pairs_.erase(
      std::remove_if(
          socket_pairs_.begin(), socket_pairs_.end(),
          [socket_pair_ptr](const std::unique_ptr<SocketPair>& item) {
            return item.get() == socket_pair_ptr;
          }),
      socket_pairs_.end());
}

}  // namespace perfetto
