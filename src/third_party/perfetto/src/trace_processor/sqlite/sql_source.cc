/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/sqlite/sql_source.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"

#if SQLITE_VERSION_NUMBER < 3041002
// There is a bug in pre-3.41.2 versions of SQLite where sqlite3_error_offset
// can return an offset out of bounds. Make it a hard compiler error to prevent
// us from hitting this bug.
#error "SQLite version is too old."
#endif

namespace perfetto::trace_processor {

namespace {

std::pair<uint32_t, uint32_t> GetLineAndColumnForOffset(const std::string& sql,
                                                        uint32_t line,
                                                        uint32_t column,
                                                        uint32_t offset) {
  if (offset == 0) {
    return std::make_pair(line, column);
  }

  const char* new_start = sql.c_str() + offset;
  size_t prev_nl = sql.rfind('\n', offset - 1);
  int64_t nl_count = std::count(sql.c_str(), new_start, '\n');
  PERFETTO_DCHECK((nl_count == 0) == (prev_nl == std::string_view::npos));

  if (prev_nl == std::string::npos) {
    return std::make_pair(line + static_cast<uint32_t>(nl_count),
                          column + static_cast<uint32_t>(offset));
  }

  int64_t new_column = std::distance(sql.c_str() + prev_nl, new_start);
  return std::make_pair(line + static_cast<uint32_t>(nl_count),
                        static_cast<uint32_t>(new_column));
}

std::pair<std::string, size_t> SqlContextAndCaretPos(const std::string& sql,
                                                     uint32_t offset) {
  PERFETTO_DCHECK(offset <= sql.size());

  // Go back 128 characters, until the start of the string or the start of the
  // line (which we encounter first).
  size_t start_idx = offset - std::min<size_t>(128ul, offset);
  if (offset > 0) {
    size_t prev_nl = sql.rfind('\n', offset - 1);
    if (prev_nl != std::string::npos) {
      start_idx = std::max(prev_nl + 1, start_idx);
    }
  }

  // Go forward 128 characters, to the end of the string or the end of the
  // line (which we encounter first).
  size_t end_idx = std::min<size_t>(offset + 128ul, sql.size());
  size_t next_nl = sql.find('\n', offset);
  if (next_nl != std::string::npos) {
    end_idx = std::min(next_nl, end_idx);
  }
  return std::make_pair(sql.substr(start_idx, end_idx - start_idx),
                        offset - start_idx);
}

}  // namespace

SqlSource::SqlSource() = default;
SqlSource::SqlSource(Node node) : root_(std::move(node)) {}

SqlSource::SqlSource(std::string sql,
                     std::string name,
                     bool include_traceback_header) {
  root_.name = std::move(name);
  root_.original_sql = sql;
  root_.rewritten_sql = std::move(sql);
  root_.include_traceback_header = include_traceback_header;
}

SqlSource SqlSource::FromExecuteQuery(std::string sql) {
  return {std::move(sql), "File \"stdin\"", true};
}

SqlSource SqlSource::FromMetric(std::string sql, const std::string& name) {
  return {std::move(sql), "Metric \"" + name + "\"", true};
}

SqlSource SqlSource::FromMetricFile(std::string sql, const std::string& name) {
  return {std::move(sql), "Metric file \"" + name + "\"", false};
}

SqlSource SqlSource::FromModuleInclude(std::string sql,
                                       const std::string& module) {
  return {std::move(sql), "Module include \"" + module + "\"", false};
}

SqlSource SqlSource::FromTraceProcessorImplementation(std::string sql) {
  return {std::move(sql), "Trace Processor Internal", false};
}

std::string SqlSource::AsTraceback(uint32_t offset) const {
  return root_.AsTraceback(offset);
}

std::string SqlSource::AsTracebackForSqliteOffset(
    std::optional<uint32_t> opt_offset) const {
  uint32_t offset = opt_offset.value_or(0);
  // It's possible for SQLite in rare cases to return an out-of-bounds
  // offset. This has been reported upstream; for now workaround this
  // by using zero as the offset if it's out of bounds.
  if (offset > sql().size()) {
    offset = 0;
  }
  return AsTraceback(offset);
}

SqlSource SqlSource::Substr(uint32_t offset, uint32_t len) const {
  SqlSource source;
  source.root_ = root_.Substr(offset, len);
  return source;
}

SqlSource SqlSource::RewriteAllIgnoreExisting(SqlSource source) const {
  // Reset any rewrites.
  SqlSource copy = *this;
  copy.root_.rewritten_sql = copy.root_.original_sql;
  copy.root_.rewrites.clear();

  SqlSource::Rewriter rewriter(std::move(copy));
  rewriter.Rewrite(0, static_cast<uint32_t>(root_.original_sql.size()),
                   std::move(source));
  return std::move(rewriter).Build();
}

std::string SqlSource::ApplyRewrites(const std::string& original_sql,
                                     const std::vector<Rewrite>& rewrites) {
  std::string sql;
  uint32_t prev_idx = 0;
  for (const auto& rewrite : rewrites) {
    PERFETTO_CHECK(prev_idx <= rewrite.original_sql_start);
    sql.append(
        original_sql.substr(prev_idx, rewrite.original_sql_start - prev_idx));
    sql.append(rewrite.rewrite_node.rewritten_sql);
    prev_idx = rewrite.original_sql_end;
  }
  sql.append(original_sql.substr(prev_idx, original_sql.size() - prev_idx));
  return sql;
}

std::string SqlSource::Node::AsTraceback(uint32_t rewritten_offset) const {
  PERFETTO_CHECK(rewritten_offset <= rewritten_sql.size());
  uint32_t original_offset = RewrittenOffsetToOriginalOffset(rewritten_offset);
  std::string res = SelfTraceback(rewritten_offset, original_offset);
  if (auto opt_idx = RewriteForOriginalOffset(original_offset); opt_idx) {
    const Rewrite& rewrite = rewrites[*opt_idx];
    PERFETTO_CHECK(rewritten_offset >= rewrite.rewritten_sql_start);
    PERFETTO_CHECK(rewritten_offset < rewrite.rewritten_sql_end);
    res.append(rewrite.rewrite_node.AsTraceback(rewritten_offset -
                                                rewrite.rewritten_sql_start));
  }
  return res;
}

std::string SqlSource::Node::SelfTraceback(uint32_t rewritten_offset,
                                           uint32_t original_offset) const {
  PERFETTO_DCHECK(original_offset <= original_sql.size());
  auto [o_context, o_caret_pos] =
      SqlContextAndCaretPos(original_sql, original_offset);
  std::string header;
  if (include_traceback_header) {
    if (!rewrites.empty()) {
      auto [r_context, r_caret_pos] =
          SqlContextAndCaretPos(rewritten_sql, rewritten_offset);
      std::string caret = std::string(r_caret_pos, ' ') + "^";
      base::StackString<1024> str("Fully expanded statement\n  %s\n  %s\n",
                                  r_context.c_str(), caret.c_str());
      header.append(str.c_str());
    }
    header += "Traceback (most recent call last):\n";
  }

  auto line_and_col =
      GetLineAndColumnForOffset(original_sql, line, col, original_offset);
  std::string caret = std::string(o_caret_pos, ' ') + "^";
  base::StackString<1024> str("%s  %s line %u col %u\n    %s\n    %s\n",
                              header.c_str(), name.c_str(), line_and_col.first,
                              line_and_col.second, o_context.c_str(),
                              caret.c_str());
  return str.ToStdString();
}

SqlSource::Node SqlSource::Node::Substr(uint32_t offset, uint32_t len) const {
  uint32_t offset_end = offset + len;
  PERFETTO_CHECK(offset_end <= rewritten_sql.size());

  uint32_t original_offset_start = RewrittenOffsetToOriginalOffset(offset);
  uint32_t original_offset_end = RewrittenOffsetToOriginalOffset(offset_end);
  std::vector<Rewrite> new_rewrites;
  for (const Rewrite& rewrite : rewrites) {
    if (offset >= rewrite.rewritten_sql_end) {
      continue;
    }
    if (offset_end < rewrite.rewritten_sql_start) {
      break;
    }
    // Special case: when the end of the substr is in the middle of a rewrite,
    // we actually want to capture the original SQL up to the end of the
    // rewrite, not just to the start as |ChildRewrittenOffset| returns.
    if (offset_end < rewrite.rewritten_sql_end) {
      original_offset_end = rewrite.original_sql_end;
    }
    uint32_t bounded_start = std::max(offset, rewrite.rewritten_sql_start);
    uint32_t bounded_end = std::min(offset_end, rewrite.rewritten_sql_end);

    uint32_t nested_start = bounded_start - rewrite.rewritten_sql_start;
    uint32_t nested_len = bounded_end - bounded_start;

    new_rewrites.push_back(Rewrite{
        rewrite.original_sql_start - original_offset_start,
        rewrite.original_sql_end - original_offset_start,
        bounded_start - offset,
        bounded_end - offset,
        rewrite.rewrite_node.Substr(nested_start, nested_len),
    });
  }
  std::string new_original = original_sql.substr(
      original_offset_start, original_offset_end - original_offset_start);
  std::string new_rewritten = rewritten_sql.substr(offset, len);
  PERFETTO_DCHECK(ApplyRewrites(new_original, new_rewrites) == new_rewritten);

  auto line_and_col =
      GetLineAndColumnForOffset(original_sql, line, col, original_offset_start);
  return Node{
      name,
      include_traceback_header,
      line_and_col.first,
      line_and_col.second,
      new_original,
      std::move(new_rewrites),
      new_rewritten,
  };
}

uint32_t SqlSource::Node::RewrittenOffsetToOriginalOffset(
    uint32_t rewritten_offset) const {
  uint32_t remaining = rewritten_offset;
  for (const Rewrite& rewrite : rewrites) {
    if (rewritten_offset >= rewrite.rewritten_sql_end) {
      remaining -= rewrite.rewritten_sql_end - rewrite.rewritten_sql_start;
      remaining += rewrite.original_sql_end - rewrite.original_sql_start;
      continue;
    }
    if (rewritten_offset < rewrite.rewritten_sql_start) {
      break;
    }
    // IMPORTANT: if the rewritten offset is anywhere inside a rewrite, we just
    // map the original offset to point to the start of the rewrite. This is
    // the only sane way we can handle arbitrary transformations of the
    // original sql.
    return rewrite.original_sql_start;
  }
  return remaining;
}

std::optional<uint32_t> SqlSource::Node::RewriteForOriginalOffset(
    uint32_t original_offset) const {
  for (uint32_t i = 0; i < rewrites.size(); ++i) {
    if (original_offset >= rewrites[i].original_sql_start &&
        original_offset < rewrites[i].original_sql_end) {
      return i;
    }
  }
  return std::nullopt;
}

SqlSource::Rewriter::Rewriter(SqlSource source)
    : Rewriter(std::move(source.root_)) {}
SqlSource::Rewriter::Rewriter(Node source) : orig_(std::move(source)) {
  // Note: it's important that we *don't* move out of |orig_| here as we want to
  // be able to access the untouched offsets through
  // calls to |RewrittenOffsetToOriginalOffset| etc.
  for (const SqlSource::Rewrite& rewrite : orig_.rewrites) {
    nested_.push_back(SqlSource::Rewriter(rewrite.rewrite_node));
  }
}

void SqlSource::Rewriter::Rewrite(uint32_t rewritten_start,
                                  uint32_t rewritten_end,
                                  SqlSource source) {
  PERFETTO_CHECK(rewritten_start <= rewritten_end);
  PERFETTO_CHECK(rewritten_end <= orig_.rewritten_sql.size());

  uint32_t original_start =
      orig_.RewrittenOffsetToOriginalOffset(rewritten_start);
  std::optional<uint32_t> maybe_rewrite =
      orig_.RewriteForOriginalOffset(original_start);
  if (maybe_rewrite) {
    const SqlSource::Rewrite& rewrite = orig_.rewrites[*maybe_rewrite];
    nested_[*maybe_rewrite].Rewrite(
        rewritten_start - rewrite.rewritten_sql_start,
        rewritten_end - rewrite.rewritten_sql_start, std::move(source));
  } else {
    uint32_t original_end =
        orig_.RewrittenOffsetToOriginalOffset(rewritten_end);
    non_nested_.push_back(SqlSource::Rewrite{
        original_start,
        original_end,
        std::numeric_limits<uint32_t>::max(),  // Dummy, corrected in |Build|.
        std::numeric_limits<uint32_t>::max(),  // Dummy, corrected in |Build|.
        std::move(source.root_),
    });
  }
}

SqlSource SqlSource::Rewriter::Build() && {
  // Phase 1: finalize all the nested rewrites and merge both nested and
  // non-nested into a single vector.
  std::vector<SqlSource::Rewrite> all_rewrites = std::move(non_nested_);
  for (uint32_t i = 0; i < nested_.size(); ++i) {
    const SqlSource::Rewrite orig_rewrite = orig_.rewrites[i];
    all_rewrites.push_back(SqlSource::Rewrite{
        orig_rewrite.original_sql_start,
        orig_rewrite.original_sql_end,
        std::numeric_limits<uint32_t>::max(),  // Dummy, corrected in phase 3.
        std::numeric_limits<uint32_t>::max(),  // Dummy, corrected in phase 3.
        std::move(nested_[i]).Build().root_,
    });
  }

  // Phase 2: sort the new rewrite vector by original offset and verify that the
  // original offsets are monotonic and non-overlapping.
  std::sort(all_rewrites.begin(), all_rewrites.end(),
            [](const SqlSource::Rewrite& a, const SqlSource::Rewrite& b) {
              return a.original_sql_start < b.original_sql_start;
            });
  for (uint32_t i = 1; i < all_rewrites.size(); ++i) {
    PERFETTO_CHECK(all_rewrites[i - 1].original_sql_end <=
                   all_rewrites[i].original_sql_start);
  }

  // Phase 3: compute the new rewritten offsets and assign them to the rewrites.
  // Also unset the traceback flag for all rewrites.
  uint32_t original_bytes_in_rewrites = 0;
  uint32_t rewritten_bytes_in_rewrites = 0;
  for (SqlSource::Rewrite& rewrite : all_rewrites) {
    uint32_t source_size =
        static_cast<uint32_t>(rewrite.rewrite_node.rewritten_sql.size());

    rewrite.rewritten_sql_start = rewrite.original_sql_start +
                                  rewritten_bytes_in_rewrites -
                                  original_bytes_in_rewrites;
    rewrite.rewritten_sql_end = rewrite.rewritten_sql_start + source_size;
    rewrite.rewrite_node.include_traceback_header = false;

    original_bytes_in_rewrites +=
        rewrite.original_sql_end - rewrite.original_sql_start;
    rewritten_bytes_in_rewrites += source_size;
  }

  // Phase 4: update the node to reflect the new rewrites.
  orig_.rewrites = std::move(all_rewrites);
  orig_.rewritten_sql = ApplyRewrites(orig_.original_sql, orig_.rewrites);
  return SqlSource(std::move(orig_));
}

}  // namespace perfetto::trace_processor
