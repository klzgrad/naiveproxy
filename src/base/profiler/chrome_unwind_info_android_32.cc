// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/chrome_unwind_info_android_32.h"

#include "base/containers/buffer_iterator.h"

namespace base {

ChromeUnwindInfoAndroid32::ChromeUnwindInfoAndroid32(
    span<const uint8_t> unwind_instruction_table,
    span<const uint8_t> function_offset_table,
    span<const FunctionTableEntry> function_table,
    span<const uint32_t> page_table)
    : unwind_instruction_table(unwind_instruction_table),
      function_offset_table(function_offset_table),
      function_table(function_table),
      page_table(page_table) {}

ChromeUnwindInfoAndroid32::~ChromeUnwindInfoAndroid32() = default;
ChromeUnwindInfoAndroid32::ChromeUnwindInfoAndroid32(
    const ChromeUnwindInfoAndroid32& other) = default;
ChromeUnwindInfoAndroid32& ChromeUnwindInfoAndroid32::operator=(
    const ChromeUnwindInfoAndroid32& other) = default;

ChromeUnwindInfoAndroid32::ChromeUnwindInfoAndroid32(
    ChromeUnwindInfoAndroid32&& other) = default;
ChromeUnwindInfoAndroid32& ChromeUnwindInfoAndroid32::operator=(
    ChromeUnwindInfoAndroid32&& other) = default;

ChromeUnwindInfoAndroid32 CreateChromeUnwindInfoAndroid32(
    span<const uint8_t> data) {
  BufferIterator<const uint8_t> data_iterator(data);

  const auto* header = data_iterator.Object<ChromeUnwindInfoAndroid32Header>();
  DCHECK(header);

  data_iterator.Seek(header->page_table_byte_offset);
  const auto page_table =
      data_iterator.Span<uint32_t>(header->page_table_entries);
  DCHECK(!page_table.empty());

  data_iterator.Seek(header->function_offset_table_byte_offset);
  const auto function_offset_table =
      data_iterator.Span<uint8_t>(header->function_offset_table_size_in_bytes);
  DCHECK(!function_offset_table.empty());

  data_iterator.Seek(header->function_table_byte_offset);
  const auto function_table =
      data_iterator.Span<FunctionTableEntry>(header->function_table_entries);
  DCHECK(!function_table.empty());

  data_iterator.Seek(header->unwind_instruction_table_byte_offset);
  const auto unwind_instruction_table = data_iterator.Span<uint8_t>(
      header->unwind_instruction_table_size_in_bytes);
  DCHECK(!unwind_instruction_table.empty());

  return ChromeUnwindInfoAndroid32{unwind_instruction_table,
                                   function_offset_table, function_table,
                                   page_table};
}

}  // namespace base
