#include "net/third_party/quic/core/qpack/qpack_header_table.h"

#include "base/logging.h"
#include "net/third_party/spdy/core/hpack/hpack_constants.h"
#include "net/third_party/spdy/core/hpack/hpack_entry.h"
#include "net/third_party/spdy/core/hpack/hpack_static_table.h"

namespace quic {

// Currently using HPACK static tables.
// TODO(bnc):  QPACK is likely to get its own static table.  When this happens,
// fork HpackStaticTable code and modify static table.
QpackHeaderTable::QpackHeaderTable()
    : static_entries_(spdy::ObtainHpackStaticTable().GetStaticEntries()),
      static_index_(spdy::ObtainHpackStaticTable().GetStaticIndex()),
      static_name_index_(spdy::ObtainHpackStaticTable().GetStaticNameIndex()) {}

QpackHeaderTable::~QpackHeaderTable() = default;

const spdy::HpackEntry* QpackHeaderTable::LookupEntry(size_t index) const {
  // Static table indexing starts with 1.
  if (index == 0 || index > static_entries_.size()) {
    return nullptr;
  }

  return &static_entries_[index - 1];
}

QpackHeaderTable::MatchType QpackHeaderTable::FindHeaderField(
    QuicStringPiece name,
    QuicStringPiece value,
    size_t* index) const {
  spdy::HpackEntry query(name, value);
  auto static_index_it = static_index_.find(&query);
  if (static_index_it != static_index_.end()) {
    DCHECK((*static_index_it)->IsStatic());
    // Static table indexing starts with 1.
    *index = (*static_index_it)->InsertionIndex() + 1;
    return MatchType::kNameAndValue;
  }

  auto static_name_index_it = static_name_index_.find(name);
  if (static_name_index_it != static_name_index_.end()) {
    DCHECK(static_name_index_it->second->IsStatic());
    // Static table indexing starts with 1.
    *index = static_name_index_it->second->InsertionIndex() + 1;
    return MatchType::kName;
  }

  return MatchType::kNoMatch;
}

}  // namespace quic
