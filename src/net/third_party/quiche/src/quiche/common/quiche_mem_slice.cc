#include "quiche/common/quiche_mem_slice.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

QuicheMemSlice::QuicheMemSlice(QuicheBuffer buffer)
    : data_(buffer.data()), size_(buffer.size()) {
  QuicheUniqueBufferPtr owned = buffer.Release();
  QuicheBufferAllocator* allocator = owned.get_deleter().allocator();
  owned.release();
  done_callback_ = [allocator](absl::string_view ptr) {
    allocator->Delete(const_cast<char*>(ptr.data()));
  };
}

QuicheMemSlice::QuicheMemSlice(std::unique_ptr<char[]> buffer, size_t length)
    : data_(buffer.release()),
      size_(length),
      done_callback_(+[](absl::string_view ptr) { delete[] ptr.data(); }) {}

QuicheMemSlice::QuicheMemSlice(
    const char* buffer, size_t length,
    SingleUseCallback<void(absl::string_view)> done_callback)
    : data_(buffer), size_(length), done_callback_(std::move(done_callback)) {}

// Move constructors. |other| will not hold a reference to the data buffer
// after this call completes.
QuicheMemSlice::QuicheMemSlice(QuicheMemSlice&& other) {
  data_ = other.data_;
  size_ = other.size_;
  done_callback_ = std::move(other.done_callback_);
  other.data_ = nullptr;
  other.size_ = 0;
  other.done_callback_ = nullptr;
}
QuicheMemSlice& QuicheMemSlice::operator=(QuicheMemSlice&& other) {
  Reset();
  data_ = other.data_;
  size_ = other.size_;
  done_callback_ = std::move(other.done_callback_);
  other.data_ = nullptr;
  other.size_ = 0;
  other.done_callback_ = nullptr;
  return *this;
}

QuicheMemSlice::~QuicheMemSlice() { Reset(); }

void QuicheMemSlice::Reset() {
  if (done_callback_ && data_ != nullptr) {
    std::move(done_callback_)(AsStringView());
  }
  data_ = nullptr;
  size_ = 0;
  done_callback_ = nullptr;
}

QuicheMemSlice QuicheMemSlice::Copy(absl::string_view data) {
  if (data.empty()) {
    return QuicheMemSlice();
  }
  auto buffer = std::make_unique<char[]>(data.size());
  memcpy(buffer.get(), data.data(), data.size());
  return QuicheMemSlice(std::move(buffer), data.size());
}

}  // namespace quiche
