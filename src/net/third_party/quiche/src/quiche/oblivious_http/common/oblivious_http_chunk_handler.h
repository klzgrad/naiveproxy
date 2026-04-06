#ifndef QUICHE_OBLIVIOUS_HTTP_COMMON_CHUNK_HANDLER_H_
#define QUICHE_OBLIVIOUS_HTTP_COMMON_CHUNK_HANDLER_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Methods to be invoked upon decryption of request/response OHTTP chunks.
class QUICHE_EXPORT ObliviousHttpChunkHandler {
 public:
  virtual ~ObliviousHttpChunkHandler() = default;
  // This method is invoked once a chunk of data has been decrypted. It returns
  // a Status to allow the implementation to signal a potential error, such as a
  // decoding issue with the decrypted data.
  virtual absl::Status OnDecryptedChunk(absl::string_view decrypted_chunk) = 0;
  // This method is invoked once all chunks have been decrypted. It returns
  // a Status to allow the implementation to signal a potential error.
  virtual absl::Status OnChunksDone() = 0;
};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_COMMON_CHUNK_HANDLER_H_
