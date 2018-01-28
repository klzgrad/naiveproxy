// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_BLOCKFILE_WEBFONTS_HISTOGRAM_H_
#define NET_DISK_CACHE_BLOCKFILE_WEBFONTS_HISTOGRAM_H_

#include <string>

namespace disk_cache {

class EntryImpl;

// A collection of functions for histogram reporting about web fonts.
namespace web_fonts_histogram {

void RecordCacheMiss(const std::string& key);
void RecordEvictedEntry(const std::string& key);
void RecordCacheHit(EntryImpl* entry);
void RecordEviction(EntryImpl* entry);

}  // namespace web_fonts_histogram

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_BLOCKFILE_WEBFONTS_HISTOGRAM_H_
