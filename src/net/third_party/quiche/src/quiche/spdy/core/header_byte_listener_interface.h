#ifndef QUICHE_SPDY_CORE_HEADER_BYTE_LISTENER_INTERFACE_H_
#define QUICHE_SPDY_CORE_HEADER_BYTE_LISTENER_INTERFACE_H_

#include <stddef.h>

#include "quiche/common/platform/api/quiche_export.h"

namespace spdy {

// Listens for the receipt of uncompressed header bytes.
class QUICHE_EXPORT HeaderByteListenerInterface {
 public:
  virtual ~HeaderByteListenerInterface() {}

  // Called when a header block has been parsed, with the number of uncompressed
  // header bytes parsed from the header block.
  virtual void OnHeaderBytesReceived(size_t uncompressed_header_bytes) = 0;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HEADER_BYTE_LISTENER_INTERFACE_H_
