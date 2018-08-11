// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/memory_infra_background_whitelist.h"

#include <ctype.h>
#include <string.h>

#include <string>

#include "base/strings/string_util.h"

namespace base {
namespace trace_event {
namespace {

// The names of dump providers whitelisted for background tracing. Dump
// providers can be added here only if the background mode dump has very
// little processor and memory overhead.
// TODO(ssid): Some dump providers do not create ownership edges on background
// dump. So, the effective size will not be correct.
const char* const kDumpProviderWhitelist[] = {
    "android::ResourceManagerImpl",
    "AutocompleteController",
    "BlinkGC",
    "BlinkObjectCounters",
    "BlobStorageContext",
    "ClientDiscardableSharedMemoryManager",
    "DOMStorage",
    "DownloadService",
    "DiscardableSharedMemoryManager",
    "gpu::BufferManager",
    "gpu::RenderbufferManager",
    "gpu::TextureManager",
    "FontCaches",
    "HistoryReport",
    "IPCChannel",
    "IndexedDBBackingStore",
    "InMemoryURLIndex",
    "JavaHeap",
    "LevelDB",
    "LeveldbValueStore",
    "LocalStorage",
    "Malloc",
    "MemoryCache",
    "MojoHandleTable",
    "MojoLevelDB",
    "MojoMessages",
    "PartitionAlloc",
    "ProcessMemoryMetrics",
    "RenderProcessHost",
    "SharedMemoryTracker",
    "Skia",
    "Sql",
    "URLRequestContext",
    "V8Isolate",
    "SyncDirectory",
    "TabRestoreServiceHelper",
    nullptr  // End of list marker.
};

// A list of string names that are allowed for the memory allocator dumps in
// background mode.
const char* const kAllocatorDumpNameWhitelist[] = {
    "blink_gc",
    "blink_gc/allocated_objects",
    "blink_objects/AdSubframe",
    "blink_objects/AudioHandler",
    "blink_objects/DetachedScriptState",
    "blink_objects/Document",
    "blink_objects/Frame",
    "blink_objects/JSEventListener",
    "blink_objects/LayoutObject",
    "blink_objects/MediaKeySession",
    "blink_objects/MediaKeys",
    "blink_objects/Node",
    "blink_objects/Resource",
    "blink_objects/RTCPeerConnection",
    "blink_objects/ScriptPromise",
    "blink_objects/PausableObject",
    "blink_objects/V8PerContextData",
    "blink_objects/WorkerGlobalScope",
    "blink_objects/UACSSResource",
    "blink_objects/ResourceFetcher",
    "components/download/controller_0x?",
    "discardable",
    "discardable/child_0x?",
    "extensions/value_store/Extensions.Database.Open.Settings/0x?",
    "extensions/value_store/Extensions.Database.Open.Rules/0x?",
    "extensions/value_store/Extensions.Database.Open.State/0x?",
    "extensions/value_store/Extensions.Database.Open/0x?",
    "extensions/value_store/Extensions.Database.Restore/0x?",
    "extensions/value_store/Extensions.Database.Value.Restore/0x?",
    "font_caches/font_platform_data_cache",
    "font_caches/shape_caches",
    "gpu/gl/buffers/share_group_0x?",
    "gpu/gl/renderbuffers/share_group_0x?",
    "gpu/gl/textures/share_group_0x?",
    "history/delta_file_service/leveldb_0x?",
    "history/usage_reports_buffer/leveldb_0x?",
    "java_heap",
    "java_heap/allocated_objects",
    "leveldatabase",
    "leveldatabase/block_cache/browser",
    "leveldatabase/block_cache/in_memory",
    "leveldatabase/block_cache/unified",
    "leveldatabase/block_cache/web",
    "leveldatabase/db_0x?",
    "leveldatabase/db_0x?/block_cache",
    "leveldatabase/memenv_0x?",
    "malloc",
    "malloc/allocated_objects",
    "malloc/metadata_fragmentation_caches",
    "mojo",
    "mojo/data_pipe_consumer",
    "mojo/data_pipe_producer",
    "mojo/messages",
    "mojo/message_pipe",
    "mojo/platform_handle",
    "mojo/queued_ipc_channel_message/0x?",
    "mojo/render_process_host/0x?",
    "mojo/shared_buffer",
    "mojo/unknown",
    "mojo/watcher",
    "net/http_network_session_0x?",
    "net/http_network_session_0x?/quic_stream_factory",
    "net/http_network_session_0x?/socket_pool",
    "net/http_network_session_0x?/spdy_session_pool",
    "net/http_network_session_0x?/stream_factory",
    "net/ssl_session_cache",
    "net/url_request_context",
    "net/url_request_context/app_request",
    "net/url_request_context/app_request/0x?",
    "net/url_request_context/app_request/0x?/cookie_monster",
    "net/url_request_context/app_request/0x?/cookie_monster/cookies",
    "net/url_request_context/app_request/0x?/cookie_monster/"
    "tasks_pending_global",
    "net/url_request_context/app_request/0x?/cookie_monster/"
    "tasks_pending_for_key",
    "net/url_request_context/app_request/0x?/http_cache",
    "net/url_request_context/app_request/0x?/http_cache/memory_backend",
    "net/url_request_context/app_request/0x?/http_cache/simple_backend",
    "net/url_request_context/app_request/0x?/http_network_session",
    "net/url_request_context/extensions",
    "net/url_request_context/extensions/0x?",
    "net/url_request_context/extensions/0x?/cookie_monster",
    "net/url_request_context/extensions/0x?/cookie_monster/cookies",
    "net/url_request_context/extensions/0x?/cookie_monster/"
    "tasks_pending_global",
    "net/url_request_context/extensions/0x?/cookie_monster/"
    "tasks_pending_for_key",
    "net/url_request_context/extensions/0x?/http_cache",
    "net/url_request_context/extensions/0x?/http_cache/memory_backend",
    "net/url_request_context/extensions/0x?/http_cache/simple_backend",
    "net/url_request_context/extensions/0x?/http_network_session",
    "net/url_request_context/isolated_media",
    "net/url_request_context/isolated_media/0x?",
    "net/url_request_context/isolated_media/0x?/cookie_monster",
    "net/url_request_context/isolated_media/0x?/cookie_monster/cookies",
    "net/url_request_context/isolated_media/0x?/cookie_monster/"
    "tasks_pending_global",
    "net/url_request_context/isolated_media/0x?/cookie_monster/"
    "tasks_pending_for_key",
    "net/url_request_context/isolated_media/0x?/http_cache",
    "net/url_request_context/isolated_media/0x?/http_cache/memory_backend",
    "net/url_request_context/isolated_media/0x?/http_cache/simple_backend",
    "net/url_request_context/isolated_media/0x?/http_network_session",
    "net/url_request_context/main",
    "net/url_request_context/main/0x?",
    "net/url_request_context/main/0x?/cookie_monster",
    "net/url_request_context/main/0x?/cookie_monster/cookies",
    "net/url_request_context/main/0x?/cookie_monster/tasks_pending_global",
    "net/url_request_context/main/0x?/cookie_monster/tasks_pending_for_key",
    "net/url_request_context/main/0x?/http_cache",
    "net/url_request_context/main/0x?/http_cache/memory_backend",
    "net/url_request_context/main/0x?/http_cache/simple_backend",
    "net/url_request_context/main/0x?/http_network_session",
    "net/url_request_context/main_media",
    "net/url_request_context/main_media/0x?",
    "net/url_request_context/main_media/0x?/cookie_monster",
    "net/url_request_context/main_media/0x?/cookie_monster/cookies",
    "net/url_request_context/main_media/0x?/cookie_monster/"
    "tasks_pending_global",
    "net/url_request_context/main_media/0x?/cookie_monster/"
    "tasks_pending_for_key",
    "net/url_request_context/main_media/0x?/http_cache",
    "net/url_request_context/main_media/0x?/http_cache/memory_backend",
    "net/url_request_context/main_media/0x?/http_cache/simple_backend",
    "net/url_request_context/main_media/0x?/http_network_session",
    "net/url_request_context/proxy",
    "net/url_request_context/proxy/0x?",
    "net/url_request_context/proxy/0x?/cookie_monster",
    "net/url_request_context/proxy/0x?/cookie_monster/cookies",
    "net/url_request_context/proxy/0x?/cookie_monster/tasks_pending_global",
    "net/url_request_context/proxy/0x?/cookie_monster/tasks_pending_for_key",
    "net/url_request_context/proxy/0x?/http_cache",
    "net/url_request_context/proxy/0x?/http_cache/memory_backend",
    "net/url_request_context/proxy/0x?/http_cache/simple_backend",
    "net/url_request_context/proxy/0x?/http_network_session",
    "net/url_request_context/safe_browsing",
    "net/url_request_context/safe_browsing/0x?",
    "net/url_request_context/safe_browsing/0x?/cookie_monster",
    "net/url_request_context/safe_browsing/0x?/cookie_monster/cookies",
    "net/url_request_context/safe_browsing/0x?/cookie_monster/"
    "tasks_pending_global",
    "net/url_request_context/safe_browsing/0x?/cookie_monster/"
    "tasks_pending_for_key",
    "net/url_request_context/safe_browsing/0x?/http_cache",
    "net/url_request_context/safe_browsing/0x?/http_cache/memory_backend",
    "net/url_request_context/safe_browsing/0x?/http_cache/simple_backend",
    "net/url_request_context/safe_browsing/0x?/http_network_session",
    "net/url_request_context/system",
    "net/url_request_context/system/0x?",
    "net/url_request_context/system/0x?/cookie_monster",
    "net/url_request_context/system/0x?/cookie_monster/cookies",
    "net/url_request_context/system/0x?/cookie_monster/tasks_pending_global",
    "net/url_request_context/system/0x?/cookie_monster/tasks_pending_for_key",
    "net/url_request_context/system/0x?/http_cache",
    "net/url_request_context/system/0x?/http_cache/memory_backend",
    "net/url_request_context/system/0x?/http_cache/simple_backend",
    "net/url_request_context/system/0x?/http_network_session",
    "net/url_request_context/unknown",
    "net/url_request_context/unknown/0x?",
    "net/url_request_context/unknown/0x?/cookie_monster",
    "net/url_request_context/unknown/0x?/cookie_monster/cookies",
    "net/url_request_context/unknown/0x?/cookie_monster/tasks_pending_global",
    "net/url_request_context/unknown/0x?/cookie_monster/tasks_pending_for_key",
    "net/url_request_context/unknown/0x?/http_cache",
    "net/url_request_context/unknown/0x?/http_cache/memory_backend",
    "net/url_request_context/unknown/0x?/http_cache/simple_backend",
    "net/url_request_context/unknown/0x?/http_network_session",
    "omnibox/autocomplete_controller/0x?",
    "omnibox/in_memory_url_index/0x?",
    "web_cache/Image_resources",
    "web_cache/CSS stylesheet_resources",
    "web_cache/Script_resources",
    "web_cache/XSL stylesheet_resources",
    "web_cache/Font_resources",
    "web_cache/Other_resources",
    "partition_alloc/allocated_objects",
    "partition_alloc/partitions",
    "partition_alloc/partitions/array_buffer",
    "partition_alloc/partitions/buffer",
    "partition_alloc/partitions/fast_malloc",
    "partition_alloc/partitions/layout",
    "skia/sk_glyph_cache",
    "skia/sk_resource_cache",
    "sqlite",
    "ui/resource_manager_0x?/default_resource/0x?",
    "ui/resource_manager_0x?/tinted_resource",
    "v8/isolate_0x?/contexts/detached_context",
    "v8/isolate_0x?/contexts/native_context",
    "v8/isolate_0x?/heap_spaces",
    "v8/isolate_0x?/heap_spaces/code_space",
    "v8/isolate_0x?/heap_spaces/large_object_space",
    "v8/isolate_0x?/heap_spaces/map_space",
    "v8/isolate_0x?/heap_spaces/new_space",
    "v8/isolate_0x?/heap_spaces/old_space",
    "v8/isolate_0x?/heap_spaces/read_only_space",
    "v8/isolate_0x?/malloc",
    "v8/isolate_0x?/zapped_for_debug",
    "site_storage/blob_storage/0x?",
    "site_storage/index_db/db_0x?",
    "site_storage/index_db/memenv_0x?",
    "site_storage/localstorage/0x?/cache_size",
    "site_storage/localstorage/0x?/leveldb",
    "site_storage/session_storage/0x?",
    "site_storage/session_storage/0x?/cache_size",
    "sync/0x?/kernel",
    "sync/0x?/store",
    "sync/0x?/model_type/APP",
    "sync/0x?/model_type/APP_LIST",
    "sync/0x?/model_type/APP_NOTIFICATION",
    "sync/0x?/model_type/APP_SETTING",
    "sync/0x?/model_type/ARC_PACKAGE",
    "sync/0x?/model_type/ARTICLE",
    "sync/0x?/model_type/AUTOFILL",
    "sync/0x?/model_type/AUTOFILL_PROFILE",
    "sync/0x?/model_type/AUTOFILL_WALLET",
    "sync/0x?/model_type/BOOKMARK",
    "sync/0x?/model_type/DEVICE_INFO",
    "sync/0x?/model_type/DICTIONARY",
    "sync/0x?/model_type/EXPERIMENTS",
    "sync/0x?/model_type/EXTENSION",
    "sync/0x?/model_type/EXTENSION_SETTING",
    "sync/0x?/model_type/FAVICON_IMAGE",
    "sync/0x?/model_type/FAVICON_TRACKING",
    "sync/0x?/model_type/HISTORY_DELETE_DIRECTIVE",
    "sync/0x?/model_type/MANAGED_USER",
    "sync/0x?/model_type/MANAGED_USER_SETTING",
    "sync/0x?/model_type/MANAGED_USER_SHARED_SETTING",
    "sync/0x?/model_type/MANAGED_USER_WHITELIST",
    "sync/0x?/model_type/NIGORI",
    "sync/0x?/model_type/PASSWORD",
    "sync/0x?/model_type/PREFERENCE",
    "sync/0x?/model_type/PRINTER",
    "sync/0x?/model_type/PRIORITY_PREFERENCE",
    "sync/0x?/model_type/READING_LIST",
    "sync/0x?/model_type/SEARCH_ENGINE",
    "sync/0x?/model_type/SESSION",
    "sync/0x?/model_type/SYNCED_NOTIFICATION",
    "sync/0x?/model_type/SYNCED_NOTIFICATION_APP_INFO",
    "sync/0x?/model_type/THEME",
    "sync/0x?/model_type/TYPED_URL",
    "sync/0x?/model_type/USER_EVENT",
    "sync/0x?/model_type/WALLET_METADATA",
    "sync/0x?/model_type/WIFI_CREDENTIAL",
    "tab_restore/service_helper_0x?/entries",
    "tab_restore/service_helper_0x?/entries/tab_0x?",
    "tab_restore/service_helper_0x?/entries/window_0x?",
    "tracing/heap_profiler_blink_gc/AllocationRegister",
    "tracing/heap_profiler_malloc/AllocationRegister",
    "tracing/heap_profiler_partition_alloc/AllocationRegister",
    nullptr  // End of list marker.
};

const char* const* g_dump_provider_whitelist = kDumpProviderWhitelist;
const char* const* g_allocator_dump_name_whitelist =
    kAllocatorDumpNameWhitelist;

bool IsMemoryDumpProviderInList(const char* mdp_name, const char* const* list) {
  for (size_t i = 0; list[i] != nullptr; ++i) {
    if (strcmp(mdp_name, list[i]) == 0)
      return true;
  }
  return false;
}

}  // namespace

bool IsMemoryDumpProviderWhitelisted(const char* mdp_name) {
  return IsMemoryDumpProviderInList(mdp_name, g_dump_provider_whitelist);
}

bool IsMemoryAllocatorDumpNameWhitelisted(const std::string& name) {
  // Global dumps are explicitly whitelisted for background use.
  if (base::StartsWith(name, "global/", CompareCase::SENSITIVE)) {
    for (size_t i = strlen("global/"); i < name.size(); i++)
      if (!base::IsHexDigit(name[i]))
        return false;
    return true;
  }

  if (base::StartsWith(name, "shared_memory/", CompareCase::SENSITIVE)) {
    for (size_t i = strlen("shared_memory/"); i < name.size(); i++)
      if (!base::IsHexDigit(name[i]))
        return false;
    return true;
  }

  // Remove special characters, numbers (including hexadecimal which are marked
  // by '0x') from the given string.
  const size_t length = name.size();
  std::string stripped_str;
  stripped_str.reserve(length);
  bool parsing_hex = false;
  for (size_t i = 0; i < length; ++i) {
    if (parsing_hex && isxdigit(name[i]))
      continue;
    parsing_hex = false;
    if (i + 1 < length && name[i] == '0' && name[i + 1] == 'x') {
      parsing_hex = true;
      stripped_str.append("0x?");
      ++i;
    } else {
      stripped_str.push_back(name[i]);
    }
  }

  for (size_t i = 0; g_allocator_dump_name_whitelist[i] != nullptr; ++i) {
    if (stripped_str == g_allocator_dump_name_whitelist[i]) {
      return true;
    }
  }
  return false;
}

void SetDumpProviderWhitelistForTesting(const char* const* list) {
  g_dump_provider_whitelist = list;
}

void SetAllocatorDumpNameWhitelistForTesting(const char* const* list) {
  g_allocator_dump_name_whitelist = list;
}

}  // namespace trace_event
}  // namespace base
