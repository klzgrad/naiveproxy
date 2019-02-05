#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_CONTAINERS_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_CONTAINERS_H_

#include "net/third_party/http2/platform/impl/http2_containers_impl.h"

namespace http2 {

// Represents a double-ended queue which may be backed by a list or a flat
// circular buffer.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename T>
using Http2Deque = Http2DequeImpl<T>;

}  // namespace http2

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_CONTAINERS_H_
