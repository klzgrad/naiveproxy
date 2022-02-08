#include "http2/adapter/window_manager.h"

#include <utility>

#include "common/platform/api/quiche_bug_tracker.h"
#include "common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

WindowManager::WindowManager(size_t window_size_limit,
                             WindowUpdateListener listener)
    : limit_(window_size_limit), window_(window_size_limit), buffered_(0),
      listener_(std::move(listener)) {}

void WindowManager::OnWindowSizeLimitChange(const size_t new_limit) {
  QUICHE_VLOG(2) << "WindowManager@" << this
                 << " OnWindowSizeLimitChange from old limit of " << limit_
                 << " to new limit of " << new_limit;
  if (new_limit > limit_) {
    window_ += (new_limit - limit_);
  } else {
    QUICHE_BUG(H2 window decrease)
        << "Window size limit decrease not currently supported.";
  }
  limit_ = new_limit;
}

void WindowManager::SetWindowSizeLimit(size_t new_limit) {
  QUICHE_VLOG(2) << "WindowManager@" << this
                 << " SetWindowSizeLimit from old limit of " << limit_
                 << " to new limit of " << new_limit;
  limit_ = new_limit;
  MaybeNotifyListener();
}

bool WindowManager::MarkDataBuffered(size_t bytes) {
  QUICHE_VLOG(2) << "WindowManager@" << this << " window: " << window_
                 << " bytes: " << bytes;
  if (window_ < bytes) {
    QUICHE_VLOG(2) << "WindowManager@" << this << " window underflow "
                   << "window: " << window_ << " bytes: " << bytes;
    window_ = 0;
  } else {
    window_ -= bytes;
  }
  buffered_ += bytes;
  if (window_ == 0) {
    // If data hasn't been flushed in a while there may be space available.
    MaybeNotifyListener();
  }
  return window_ > 0;
}

void WindowManager::MarkDataFlushed(size_t bytes) {
  QUICHE_VLOG(2) << "WindowManager@" << this << " buffered: " << buffered_
                 << " bytes: " << bytes;
  if (buffered_ < bytes) {
    QUICHE_BUG(bug_2816_1) << "WindowManager@" << this << " buffered underflow "
                           << "buffered_: " << buffered_ << " bytes: " << bytes;
    buffered_ = 0;
  } else {
    buffered_ -= bytes;
  }
  MaybeNotifyListener();
}

void WindowManager::MaybeNotifyListener() {
  if (buffered_ + window_ > limit_) {
    QUICHE_LOG(ERROR) << "Flow control violation; limit: " << limit_
                      << " buffered: " << buffered_ << " window: " << window_;
    return;
  }
  // For the sake of efficiency, we want to send window updates if less than
  // half of the max quota is available to the peer at any point in time.
  const size_t kDesiredMinWindow = limit_ / 2;
  const size_t kDesiredMinDelta = limit_ / 3;
  const size_t delta = limit_ - (buffered_ + window_);
  bool send_update = false;
  if (delta >= kDesiredMinDelta) {
    // This particular window update was sent because the available delta
    // exceeded the desired minimum.
    send_update = true;
  } else if (window_ < kDesiredMinWindow) {
    // This particular window update was sent because the quota available to the
    // peer at this moment is less than the desired minimum.
    send_update = true;
  }
  if (send_update && delta > 0) {
    QUICHE_VLOG(2) << "WindowManager@" << this
                   << " Informing listener of delta: " << delta;
    listener_(delta);
    window_ += delta;
  }
}

}  // namespace adapter
}  // namespace http2
