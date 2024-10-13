#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_MEM_SLICE_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_MEM_SLICE_IMPL_H_

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <utility>

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

class QUICHE_EXPORT QuicheMemSliceImpl {
 public:
  QuicheMemSliceImpl() = default;

  explicit QuicheMemSliceImpl(QuicheBuffer buffer)
      : data_(buffer.data()), size_(buffer.size()) {
    QuicheUniqueBufferPtr owned = buffer.Release();
    QuicheBufferAllocator* allocator = owned.get_deleter().allocator();
    owned.release();
    done_callback_ = [allocator](const char* ptr) {
      allocator->Delete(const_cast<char*>(ptr));
    };
  }

  QuicheMemSliceImpl(std::unique_ptr<char[]> buffer, size_t length)
      : data_(buffer.release()),
        size_(length),
        done_callback_(+[](const char* ptr) { delete[] ptr; }) {}

  QuicheMemSliceImpl(const char* buffer, size_t length,
                     SingleUseCallback<void(const char*)> done_callback)
      : data_(buffer),
        size_(length),
        done_callback_(std::move(done_callback)) {}

  QuicheMemSliceImpl(const QuicheMemSliceImpl& other) = delete;
  QuicheMemSliceImpl& operator=(const QuicheMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicheMemSliceImpl(QuicheMemSliceImpl&& other) {
    data_ = other.data_;
    size_ = other.size_;
    done_callback_ = std::move(other.done_callback_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.done_callback_ = nullptr;
  }
  QuicheMemSliceImpl& operator=(QuicheMemSliceImpl&& other) {
    Reset();
    data_ = other.data_;
    size_ = other.size_;
    done_callback_ = std::move(other.done_callback_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.done_callback_ = nullptr;
    return *this;
  }

  ~QuicheMemSliceImpl() { Reset(); }

  void Reset() {
    if (done_callback_ && data_ != nullptr) {
      std::move(done_callback_)(data_);
    }
    data_ = nullptr;
    size_ = 0;
    done_callback_ = nullptr;
  }

  const char* data() const { return data_; }
  size_t length() const { return size_; }
  bool empty() const { return size_ == 0; }

 private:
  const char* data_;
  size_t size_;
  SingleUseCallback<void(const char*)> done_callback_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_MEM_SLICE_IMPL_H_
