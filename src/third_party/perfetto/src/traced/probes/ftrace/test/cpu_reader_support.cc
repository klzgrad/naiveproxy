/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/traced/probes/ftrace/test/cpu_reader_support.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <memory>

#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/utils.h"
#include "src/base/test/utils.h"
#include "src/traced/probes/ftrace/event_info.h"
#include "src/traced/probes/ftrace/ftrace_procfs.h"

// TODO(rsavitski): rename to "cpu_reader_test_utils" or similar.
namespace perfetto {

// Caching layer for proto translation tables used in tests
// Note that this breaks test isolation, but we rarely mutate the tables.
ProtoTranslationTable* GetTable(const std::string& name) {
  static base::NoDestructor<
      std::map<std::string, std::unique_ptr<FtraceProcfs>>>
      g_tracefs;
  static base::NoDestructor<
      std::map<std::string, std::unique_ptr<ProtoTranslationTable>>>
      g_tables;

  // return if cached
  auto it_table = g_tables.ref().find(name);
  if (it_table != g_tables.ref().end()) {
    return it_table->second.get();
  }

  PERFETTO_CHECK(!g_tracefs.ref().count(name));
  std::string path = "src/traced/probes/ftrace/test/data/" + name + "/";
  struct stat st{};
  if (lstat(path.c_str(), &st) == -1 && errno == ENOENT) {
    // For OSS fuzz, which does not run in the correct cwd.
    path = base::GetTestDataPath(path);
  }
  auto [it, inserted] =
      g_tracefs.ref().emplace(name, std::make_unique<FtraceProcfs>(path));
  PERFETTO_CHECK(inserted);
  auto table = ProtoTranslationTable::Create(
      it->second.get(), GetStaticEventInfo(), GetStaticCommonFieldsInfo());
  PERFETTO_CHECK(table);
  g_tables.ref().emplace(name, std::move(table));

  return g_tables.ref().at(name).get();
}

std::unique_ptr<uint8_t[]> PageFromXxd(const std::string& text) {
  auto buffer = std::unique_ptr<uint8_t[]>(new uint8_t[base::GetSysPageSize()]);
  const char* ptr = text.data();
  memset(buffer.get(), 0xfa, base::GetSysPageSize());
  uint8_t* out = buffer.get();
  while (*ptr != '\0') {
    if (*(ptr++) != ':')
      continue;
    for (int i = 0; i < 8; i++) {
      PERFETTO_CHECK(text.size() >=
                     static_cast<size_t>((ptr - text.data()) + 5));
      PERFETTO_CHECK(*(ptr++) == ' ');
      int n = sscanf(ptr, "%02hhx%02hhx", out, out + 1);
      PERFETTO_CHECK(n == 2);
      out += n;
      ptr += 4;
    }
    while (*ptr != '\n')
      ptr++;
  }
  return buffer;
}

}  // namespace perfetto
