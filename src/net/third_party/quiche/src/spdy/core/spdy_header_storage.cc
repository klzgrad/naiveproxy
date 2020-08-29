#include "net/third_party/quiche/src/spdy/core/spdy_header_storage.h"

#include <cstring>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {
namespace {

// SpdyHeaderStorage allocates blocks of this size by default.
const size_t kDefaultStorageBlockSize = 2048;

}  // namespace

SpdyHeaderStorage::SpdyHeaderStorage() : arena_(kDefaultStorageBlockSize) {}

quiche::QuicheStringPiece SpdyHeaderStorage::Write(
    const quiche::QuicheStringPiece s) {
  return quiche::QuicheStringPiece(arena_.Memdup(s.data(), s.size()), s.size());
}

void SpdyHeaderStorage::Rewind(const quiche::QuicheStringPiece s) {
  arena_.Free(const_cast<char*>(s.data()), s.size());
}

quiche::QuicheStringPiece SpdyHeaderStorage::WriteFragments(
    const std::vector<quiche::QuicheStringPiece>& fragments,
    quiche::QuicheStringPiece separator) {
  if (fragments.empty()) {
    return quiche::QuicheStringPiece();
  }
  size_t total_size = separator.size() * (fragments.size() - 1);
  for (const quiche::QuicheStringPiece& fragment : fragments) {
    total_size += fragment.size();
  }
  char* dst = arena_.Alloc(total_size);
  size_t written = Join(dst, fragments, separator);
  DCHECK_EQ(written, total_size);
  return quiche::QuicheStringPiece(dst, total_size);
}

size_t Join(char* dst,
            const std::vector<quiche::QuicheStringPiece>& fragments,
            quiche::QuicheStringPiece separator) {
  if (fragments.empty()) {
    return 0;
  }
  auto* original_dst = dst;
  auto it = fragments.begin();
  memcpy(dst, it->data(), it->size());
  dst += it->size();
  for (++it; it != fragments.end(); ++it) {
    memcpy(dst, separator.data(), separator.size());
    dst += separator.size();
    memcpy(dst, it->data(), it->size());
    dst += it->size();
  }
  return dst - original_dst;
}

}  // namespace spdy
