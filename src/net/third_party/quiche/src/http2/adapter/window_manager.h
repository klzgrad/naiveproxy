#ifndef QUICHE_HTTP2_ADAPTER_WINDOW_MANAGER_H_
#define QUICHE_HTTP2_ADAPTER_WINDOW_MANAGER_H_

#include <stddef.h>

#include <functional>

#include "common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

namespace test {
class WindowManagerPeer;
}

// This class keeps track of a HTTP/2 flow control window, notifying a listener
// when a window update needs to be sent. This class is not thread-safe.
class QUICHE_EXPORT_PRIVATE WindowManager {
 public:
  // A WindowUpdateListener is invoked when it is time to send a window update.
  typedef std::function<void(size_t)> WindowUpdateListener;

  WindowManager(size_t window_size_limit,
                WindowUpdateListener listener);

  size_t CurrentWindowSize() const { return window_; }
  size_t WindowSizeLimit() const { return limit_; }

  // Called when the window size limit is changed (typically via settings) but
  // no window update should be sent.
  void OnWindowSizeLimitChange(size_t new_limit);

  // Sets the window size limit to |new_limit| and notifies the listener to
  // update as necessary.
  void SetWindowSizeLimit(size_t new_limit);

  // Increments the running total of data bytes buffered. Returns true iff there
  // is more window remaining.
  bool MarkDataBuffered(size_t bytes);

  // Increments the running total of data bytes that have been flushed or
  // dropped. Invokes the listener if the current window is smaller than some
  // threshold and there is quota available to send.
  void MarkDataFlushed(size_t bytes);

  // Convenience method, used when incoming data is immediately dropped or
  // ignored.
  void MarkWindowConsumed(size_t bytes) {
    MarkDataBuffered(bytes);
    MarkDataFlushed(bytes);
  }

 private:
  friend class test::WindowManagerPeer;

  void MaybeNotifyListener();

  // The upper bound on the flow control window. The GFE attempts to maintain a
  // window of this size at the peer as data is proxied through.
  size_t limit_;

  // The current flow control window that has not been advertised to the peer
  // and not yet consumed. The peer can send this many bytes before becoming
  // blocked.
  size_t window_;

  // The amount of data already buffered, which should count against the flow
  // control window upper bound.
  size_t buffered_;

  WindowUpdateListener listener_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_WINDOW_MANAGER_H_
