#ifndef QUICHE_COMMON_QUICHE_DEFAULT_MEM_SLICE_IMPL_H_
#define QUICHE_COMMON_QUICHE_DEFAULT_MEM_SLICE_IMPL_H_

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <utility>

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// The default (and soon, hopefully the only) implementation of QuicheMemSlice.
class QUICHE_EXPORT QuicheDefaultMemSliceImpl {
 public:
  QuicheDefaultMemSliceImpl() = default;

  explicit QuicheDefaultMemSliceImpl(QuicheBuffer buffer)
      : data_(buffer.data()), size_(buffer.size()) {
    QuicheUniqueBufferPtr owned = buffer.Release();
    QuicheBufferAllocator* allocator = owned.get_deleter().allocator();
    owned.release();
    done_callback_ = [allocator](const char* ptr) {
      allocator->Delete(const_cast<char*>(ptr));
    };
  }

  QuicheDefaultMemSliceImpl(std::unique_ptr<char[]> buffer, size_t length)
      : data_(buffer.release()),
        size_(length),
        done_callback_(+[](const char* ptr) { delete[] ptr; }) {}

  QuicheDefaultMemSliceImpl(const char* buffer, size_t length,
                            SingleUseCallback<void(const char*)> done_callback)
      : data_(buffer),
        size_(length),
        done_callback_(std::move(done_callback)) {}

  QuicheDefaultMemSliceImpl(const QuicheDefaultMemSliceImpl& other) = delete;
  QuicheDefaultMemSliceImpl& operator=(const QuicheDefaultMemSliceImpl& other) =
      delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicheDefaultMemSliceImpl(QuicheDefaultMemSliceImpl&& other) {
    data_ = other.data_;
    size_ = other.size_;
    done_callback_ = std::move(other.done_callback_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.done_callback_ = nullptr;
  }
  QuicheDefaultMemSliceImpl& operator=(QuicheDefaultMemSliceImpl&& other) {
    Reset();
    data_ = other.data_;
    size_ = other.size_;
    done_callback_ = std::move(other.done_callback_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.done_callback_ = nullptr;
    return *this;
  }

  ~QuicheDefaultMemSliceImpl() { Reset(); }

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
  const char* data_ = nullptr;
  size_t size_ = 0;
  SingleUseCallback<void(const char*)> done_callback_ = nullptr;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_DEFAULT_MEM_SLICE_IMPL_H_
