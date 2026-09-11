/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/read_trace_internal.h"

#include <fcntl.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"

// URL fetching shells out to `curl` via base::Subprocess. Keep in sync with
// `_subprocess_supported` in src/base/BUILD.gn; elsewhere we error out cleanly.
#define PERFETTO_TP_HTTP_IMPORT_SUPPORTED()             \
  (PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
   PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) ||           \
   PERFETTO_BUILDFLAG(PERFETTO_OS_MAC) ||               \
   PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) ||           \
   PERFETTO_BUILDFLAG(PERFETTO_OS_WIN))

#if PERFETTO_TP_HTTP_IMPORT_SUPPORTED()
#include "perfetto/base/proc_utils.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/pipe.h"
#include "perfetto/ext/base/subprocess.h"
#include "src/trace_processor/util/simple_json_parser.h"
#endif

namespace perfetto::trace_processor {
namespace {

// 1MB chunk size seems the best tradeoff on a MacBook Pro 2013 - i7 2.8 GHz.
constexpr size_t kChunkSize = 1024 * 1024;

bool IsUrl(const std::string& path) {
  return base::StartsWith(path, "http://") ||
         base::StartsWith(path, "https://");
}

// The decomposed components of a URL of the form:
//   <scheme>://<host>[:<port>]/<path>[?<query>][#<fragment>]
struct ParsedUrl {
  std::string_view scheme;
  std::string_view host;
  std::string_view path;
  std::string_view query;
  std::string_view fragment;
};

// Parses `url` into its components. Returns nullopt if there is no "://"
// separating the scheme from the authority. `url` must outlive the result, as
// the components are views into it.
std::optional<ParsedUrl> ParseUrl(std::string_view url) {
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos)
    return std::nullopt;

  ParsedUrl out;
  out.scheme = url.substr(0, scheme_end);

  std::string_view rest = url.substr(scheme_end + 3);

  // The authority runs up to the first '/', '?' or '#'.
  size_t authority_end = rest.find_first_of("/?#");
  std::string_view authority = rest.substr(0, authority_end);
  rest = authority_end == std::string_view::npos ? std::string_view()
                                                 : rest.substr(authority_end);

  // Strip any "userinfo@" prefix and ":port" suffix to leave just the host.
  if (size_t at = authority.find('@'); at != std::string_view::npos)
    authority = authority.substr(at + 1);
  out.host = authority.substr(0, authority.find(':'));

  // The fragment is everything after the first '#'.
  if (size_t frag = rest.find('#'); frag != std::string_view::npos) {
    out.fragment = rest.substr(frag + 1);
    rest = rest.substr(0, frag);
  }
  // The query is everything between '?' and the fragment.
  if (size_t q = rest.find('?'); q != std::string_view::npos) {
    out.query = rest.substr(q + 1);
    rest = rest.substr(0, q);
  }
  out.path = rest;
  return out;
}

// Returns the value of the `key` parameter in an "a=1&b=2" style query string,
// or nullopt if it is absent.
std::optional<std::string_view> GetQueryParam(std::string_view query,
                                              std::string_view key) {
  while (!query.empty()) {
    size_t amp = query.find('&');
    std::string_view pair = query.substr(0, amp);
    size_t eq = pair.find('=');
    if (pair.substr(0, eq) == key)
      return eq == std::string_view::npos ? std::string_view()
                                          : pair.substr(eq + 1);
    if (amp == std::string_view::npos)
      break;
    query = query.substr(amp + 1);
  }
  return std::nullopt;
}

bool IsPermalinkHash(std::string_view s) {
  if (s.size() != 40)
    return false;
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return true;
}

// If `url` is a Perfetto UI share link (ui.perfetto.dev/#!/?s=<hash>), returns
// the 40-char hex permalink hash. Returns nullopt for any other URL.
std::optional<std::string> GetPermalinkHash(const std::string& url) {
  std::optional<ParsedUrl> parsed = ParseUrl(url);
  if (!parsed || parsed->host != "ui.perfetto.dev")
    return std::nullopt;

  // The share id is the `s` query param. The Perfetto UI uses hash-based
  // routing (#!/?s=<hash>), so it normally lives in the query string of the
  // fragment, but accept it in the top-level query string too.
  auto valid_hash =
      [](std::optional<std::string_view> v) -> std::optional<std::string> {
    if (v && IsPermalinkHash(*v))
      return std::string(*v);
    return std::nullopt;
  };
  if (auto h = valid_hash(GetQueryParam(parsed->query, "s")))
    return h;
  if (size_t q = parsed->fragment.find('?'); q != std::string_view::npos) {
    if (auto h = valid_hash(GetQueryParam(parsed->fragment.substr(q + 1), "s")))
      return h;
  }
  return std::nullopt;
}

#if PERFETTO_TP_HTTP_IMPORT_SUPPORTED()

// GCS bucket that backs ui.perfetto.dev share links. A permalink hash resolves
// to a small JSON object stored at <kPermalinkBucketUrl><hash>, which in turn
// references the underlying trace via its "traceUrl" field. Keep in sync with
// ui/src/base/gcs_uploader.ts and ui/src/frontend/permalink.ts.
constexpr char kPermalinkBucketUrl[] =
    "https://storage.googleapis.com/perfetto-ui-data/";

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
constexpr char kPathSep = '\\';
#else
constexpr char kPathSep = '/';
#endif

// Returns the directory used to cache traces downloaded from URLs, creating it
// (and the intermediate directories) if necessary. Returns nullopt if no
// suitable location is available or the directory can't be created, in which
// case callers proceed without caching.
//   POSIX:   $XDG_CACHE_HOME/perfetto/tp-http-traces, else ~/.cache/...
//   Windows: %LOCALAPPDATA%\perfetto\tp-http-traces
std::optional<std::string> GetTraceCacheDir() {
  std::string dir;
  auto append_and_mkdir = [&dir](std::initializer_list<const char*> parts) {
    for (const char* p : parts) {
      dir += kPathSep;
      dir += p;
      base::Mkdir(dir);  // Best-effort; ignores "already exists".
    }
  };
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  const char* root = getenv("LOCALAPPDATA");
  if (!root || !*root)
    return std::nullopt;
  dir = root;
  append_and_mkdir({"perfetto", "tp-http-traces"});
#else
  if (const char* xdg = getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    dir = xdg;
    append_and_mkdir({"perfetto", "tp-http-traces"});
  } else if (const char* home = getenv("HOME"); home && *home) {
    dir = home;
    append_and_mkdir({".cache", "perfetto", "tp-http-traces"});
  } else {
    return std::nullopt;
  }
#endif
  if (!base::FileExists(dir))
    return std::nullopt;
  return dir;
}

// Returns the path of the cache entry for `url`, or nullopt if caching is
// unavailable. The file is named by the hash of the URL we actually download
// (for permalinks, the resolved content-addressed trace URL), so the same
// trace shared via different links maps to a single cache entry.
std::optional<std::string> CachePathForUrl(const std::string& url) {
  std::optional<std::string> dir = GetTraceCacheDir();
  if (!dir)
    return std::nullopt;
  base::StackString<24> name("%016" PRIx64,
                             base::MurmurHashValue(std::string_view(url)));
  return *dir + kPathSep + name.c_str();
}

// Builds curl to fetch `url` with its stderr redirected to `stderr_wr`. The
// flags keep curl quiet: -L follows redirects, -f makes it fail (non-zero exit,
// empty body) on HTTP errors and -sS suppresses the progress meter while still
// emitting actual error messages on stderr (which we capture rather than let
// leak to the terminal).
//
// If `if_modified_since_file` is set, curl issues a conditional GET (-z), using
// that file's modification time as the If-Modified-Since value. The server then
// returns "304 Not Modified" with an empty body when the resource is unchanged,
// which lets us revalidate a cached copy without re-downloading it.
base::Subprocess MakeCurl(
    const std::string& url,
    const std::optional<std::string>& if_modified_since_file,
    base::ScopedPlatformHandle stderr_wr) {
  base::Subprocess curl;
  curl.args.exec_cmd = {"curl", "-L", "-f", "-sS"};
  if (if_modified_since_file) {
    curl.args.exec_cmd.push_back("-z");
    curl.args.exec_cmd.push_back(*if_modified_since_file);
  }
  curl.args.exec_cmd.push_back(url);
  curl.args.stdin_mode = base::Subprocess::InputMode::kDevNull;
  curl.args.stdout_mode = base::Subprocess::OutputMode::kBuffer;
  curl.args.stderr_mode = base::Subprocess::OutputMode::kFd;
  curl.args.out_fd = std::move(stderr_wr);
  return curl;
}

// Turns a failed curl invocation into a Status, preferring curl's own (now
// captured) error message and falling back to the exit code.
base::Status CurlError(const std::string& url,
                       int returncode,
                       const std::string& stderr_text) {
  std::string detail = base::TrimWhitespace(stderr_text);
  if (!detail.empty())
    return base::ErrStatus("Failed to download '%s': %s", url.c_str(),
                           detail.c_str());
  return base::ErrStatus(
      "Failed to download '%s': curl exited with code %d (is curl installed "
      "and "
      "the URL reachable?)",
      url.c_str(), returncode);
}

// Fetches `url` in full into `out`. Used for small responses (permalink JSON).
base::Status CurlFetchAll(const std::string& url, std::string* out) {
  base::Pipe err = base::Pipe::Create();
  base::Subprocess curl = MakeCurl(url, std::nullopt, std::move(err.wr));
  // Call() drains stdout into output() and waits for curl to exit; the write
  // end of `err` is closed in the parent by Start(), so the read below sees
  // EOF.
  bool ok = curl.Call();
  std::string stderr_text;
  base::ReadPlatformHandle(*err.rd, &stderr_text);
  if (!ok)
    return CurlError(url, curl.returncode(), stderr_text);
  *out = std::move(curl.output());
  return base::OkStatus();
}

// Fetches `url` and streams the bytes into `tp` as they arrive, keeping at most
// one poll interval of data in memory at a time. If `cache_path` is set, the
// bytes are also written to a temp file and atomically renamed into place once
// the whole download has been parsed successfully. If `conditional` is true the
// request is a conditional GET against the existing `cache_path` (see
// MakeCurl): the server may answer "304 Not Modified" with an empty body, in
// which case nothing is streamed and `*bytes_streamed` is left at 0 so the
// caller knows to fall back to the cached copy. On return `*bytes_streamed`
// holds the number of bytes fed into `tp`.
base::Status CurlStreamInto(
    TraceProcessor* tp,
    const std::string& url,
    const std::optional<std::string>& cache_path,
    bool conditional,
    const std::function<void(uint64_t parsed_size)>& progress_callback,
    uint64_t* bytes_streamed) {
  *bytes_streamed = 0;

  // Tee the download into a per-pid temp file alongside the final cache entry
  // so concurrent downloads of the same URL don't clobber each other. Caching
  // is best-effort: any failure here just disables it for this download.
  base::ScopedFile cache_fd;
  std::string cache_tmp;
  if (cache_path) {
    cache_tmp = *cache_path + "." +
                std::to_string(static_cast<uint64_t>(base::GetProcessId())) +
                ".tmp";
    cache_fd = base::OpenFile(cache_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!cache_fd)
      cache_tmp.clear();
  }

  // For a conditional request curl derives the If-Modified-Since time from the
  // existing cache file's mtime.
  std::optional<std::string> if_modified_since =
      conditional ? cache_path : std::nullopt;

  base::Pipe err = base::Pipe::Create();
  base::Subprocess curl = MakeCurl(url, if_modified_since, std::move(err.wr));
  curl.Start();

  uint64_t bytes_read = 0;
  base::Status parse_status;
  bool curl_exited = false;
  for (;;) {
    // Wait() drains whatever stdout is currently available into output() and
    // returns true once curl has exited and all output has been read.
    bool done = curl.Wait(/*timeout_ms=*/100);
    std::string& out = curl.output();
    if (!out.empty()) {
      if (cache_fd && base::WriteAll(*cache_fd, out.data(), out.size()) < 0)
        cache_fd.reset();  // Stop caching, but keep streaming.
      bytes_read += out.size();
      TraceBlobView view(TraceBlob::CopyFrom(out.data(), out.size()));
      out.clear();
      parse_status = tp->Parse(std::move(view));
      if (!parse_status.ok())
        break;
      if (progress_callback)
        progress_callback(bytes_read);
    }
    if (done) {
      curl_exited = true;
      break;
    }
  }

  // If we bailed out of the loop early (parse error) curl is still running and,
  // now that we've stopped draining its stdout, will soon block on a full pipe.
  // Kill it so it releases its end of the stderr pipe; otherwise the
  // ReadPlatformHandle() below would deadlock waiting for an EOF that never
  // comes.
  if (!curl_exited)
    curl.KillAndWaitForTermination();

  // curl has exited; with -sS its stderr is at most a short error line, which
  // is now fully buffered in the pipe and read here (the parent's write end was
  // closed by Start(), so this sees EOF).
  std::string stderr_text;
  base::ReadPlatformHandle(*err.rd, &stderr_text);
  bool curl_ok =
      curl.status() == base::Subprocess::kTerminated && curl.returncode() == 0;

  // Commit the cache entry only on a fully successful download+parse that
  // actually produced bytes. A 304 Not Modified streams nothing, so committing
  // here would clobber the up-to-date cache with an empty file.
  if (!cache_tmp.empty()) {
    cache_fd.reset();  // Close before renaming.
    if (curl_ok && parse_status.ok() && bytes_read > 0 && cache_path &&
        std::rename(cache_tmp.c_str(), cache_path->c_str()) == 0) {
      // Committed.
    } else {
      remove(cache_tmp.c_str());  // 304, lost a race, or download failed.
    }
  }

  RETURN_IF_ERROR(parse_status);
  if (!curl_ok)
    return CurlError(url, curl.returncode(), stderr_text);
  *bytes_streamed = bytes_read;
  return base::OkStatus();
}

// Resolves a Perfetto UI permalink hash to the underlying trace URL by fetching
// and parsing the permalink JSON object from GCS.
base::StatusOr<std::string> ResolvePermalink(const std::string& hash) {
  std::string json;
  RETURN_IF_ERROR(CurlFetchAll(kPermalinkBucketUrl + hash, &json));

  std::optional<std::string> trace_url;
  json::SimpleJsonParser parser(json);
  RETURN_IF_ERROR(parser.Parse());
  RETURN_IF_ERROR(
      parser.ForEachField([&](std::string_view key) -> json::FieldResult {
        if (key == "traceUrl") {
          if (auto s = parser.GetString())
            trace_url = std::string(*s);
          return json::FieldResult::Handled{};
        }
        return json::FieldResult::Skip{};
      }));

  if (!trace_url || trace_url->empty()) {
    return base::ErrStatus(
        "Perfetto UI permalink '%s' does not reference a trace (it may only "
        "contain saved UI state).",
        hash.c_str());
  }
  return *trace_url;
}

// Reads the cached copy at `cache_path` into `tp` via the normal local-file
// path (mmap etc.).
base::Status ReadCachedTrace(
    TraceProcessor* tp,
    const std::string& url,
    const std::string& cache_path,
    const std::function<void(uint64_t parsed_size)>& progress_callback) {
  RETURN_IF_ERROR(
      ReadTraceUnfinalized(tp, cache_path.c_str(), progress_callback));
  tp->SetCurrentTraceName(url);
  return base::OkStatus();
}

base::Status ReadTraceFromUrl(
    TraceProcessor* tp,
    const std::string& url,
    const std::optional<std::string>& permalink_hash,
    bool use_cache,
    const std::function<void(uint64_t parsed_size)>& progress_callback) {
  std::string trace_url = url;
  if (permalink_hash) {
    PERFETTO_LOG("Resolving Perfetto UI permalink (hash: %s)",
                 permalink_hash->c_str());
    ASSIGN_OR_RETURN(trace_url, ResolvePermalink(*permalink_hash));
  }

  std::optional<std::string> cache_path =
      use_cache ? CachePathForUrl(trace_url) : std::nullopt;
  bool cache_exists = cache_path && base::FileExists(*cache_path);

  // Always ask the server whether our cached copy (if any) is still current via
  // a conditional GET. We never serve a stale trace for a mutable URL, while a
  // "304 Not Modified" still avoids re-downloading an unchanged one.
  PERFETTO_LOG("Downloading trace from %s", trace_url.c_str());
  uint64_t streamed = 0;
  base::Status status =
      CurlStreamInto(tp, trace_url, cache_path, /*conditional=*/cache_exists,
                     progress_callback, &streamed);

  // The fetch failed (e.g. offline) but nothing was streamed into `tp` yet, so
  // it's safe to fall back to a previously cached copy rather than fail
  // outright.
  if (!status.ok()) {
    if (cache_exists && streamed == 0) {
      PERFETTO_LOG("Download failed (%s); using cached trace for %s",
                   status.c_message(), trace_url.c_str());
      return ReadCachedTrace(tp, url, *cache_path, progress_callback);
    }
    return status;
  }

  // A successful fetch that streamed no bytes is a 304 Not Modified: the cached
  // copy is current, so parse that instead.
  if (streamed == 0 && cache_exists) {
    PERFETTO_LOG("Cached trace is up to date for %s", trace_url.c_str());
    return ReadCachedTrace(tp, url, *cache_path, progress_callback);
  }

  tp->SetCurrentTraceName(url);
  return base::OkStatus();
}

#else  // !PERFETTO_TP_HTTP_IMPORT_SUPPORTED()

base::Status ReadTraceFromUrl(
    TraceProcessor*,
    const std::string& url,
    const std::optional<std::string>&,
    bool,
    const std::function<void(uint64_t parsed_size)>&) {
  return base::ErrStatus(
      "Loading traces from a URL is not supported on this platform (url: %s)",
      url.c_str());
}

#endif  // PERFETTO_TP_HTTP_IMPORT_SUPPORTED()

base::Status ReadTraceUsingRead(
    TraceProcessor* tp,
    int fd,
    uint64_t* file_size,
    const std::function<void(uint64_t parsed_size)>& progress_callback) {
  // Load the trace in chunks using ordinary read().
  for (int i = 0;; i++) {
    if (progress_callback && i % 128 == 0)
      progress_callback(*file_size);

    TraceBlob blob = TraceBlob::Allocate(kChunkSize);
    auto rsize = base::Read(fd, blob.data(), blob.size());
    if (rsize == 0)
      break;

    if (rsize < 0) {
      return base::ErrStatus("Reading trace file failed (errno: %d, %s)", errno,
                             strerror(errno));
    }

    *file_size += static_cast<uint64_t>(rsize);
    TraceBlobView blob_view(std::move(blob), 0, static_cast<size_t>(rsize));
    RETURN_IF_ERROR(tp->Parse(std::move(blob_view)));
  }
  return base::OkStatus();
}
}  // namespace

base::Status ReadTraceUnfinalized(
    TraceProcessor* tp,
    const char* filename,
    const std::function<void(uint64_t parsed_size)>& progress_callback,
    const ReadTraceArgs& args) {
  // Handle http(s) URLs, but only the kind the caller has opted into. Perfetto
  // UI share links and plain URLs are gated on separate flags; if the relevant
  // one is not set we return a clear error rather than treating the URL as a
  // (non-existent) local file path.
  if (IsUrl(filename)) {
    std::optional<std::string> permalink_hash = GetPermalinkHash(filename);
    if (permalink_hash) {
      if (!args.allow_perfetto_ui_links) {
        return base::ErrStatus(
            "Cannot load Perfetto UI share link '%s': loading share links is "
            "not enabled (set ReadTraceArgs::allow_perfetto_ui_links).",
            filename);
      }
    } else if (!args.allow_http) {
      return base::ErrStatus(
          "Cannot load '%s' from a URL: loading traces from URLs is not "
          "enabled "
          "(set ReadTraceArgs::allow_http).",
          filename);
    }
    return ReadTraceFromUrl(tp, filename, permalink_hash, args.cache_downloads,
                            progress_callback);
  }

  uint64_t bytes_read = 0;

#if PERFETTO_HAS_MMAP()
  char* no_mmap = getenv("TRACE_PROCESSOR_NO_MMAP");
  bool use_mmap = !no_mmap || *no_mmap != '1';

  if (use_mmap) {
    base::ScopedMmap mapped = base::ReadMmapWholeFile(filename);
    if (mapped.IsValid()) {
      size_t length = mapped.length();
      TraceBlobView whole_mmap(TraceBlob::FromMmap(std::move(mapped)));
      // Parse the file in chunks so we get some status update on stdio.
      static constexpr size_t kMmapChunkSize = 128ul * 1024 * 1024;
      while (bytes_read < length) {
        progress_callback(bytes_read);
        const size_t bytes_read_z = static_cast<size_t>(bytes_read);
        size_t slice_size = std::min(length - bytes_read_z, kMmapChunkSize);
        TraceBlobView slice = whole_mmap.slice_off(bytes_read_z, slice_size);
        RETURN_IF_ERROR(tp->Parse(std::move(slice)));
        bytes_read += slice_size;
      }  // while (slices)
    }  // if (mapped.IsValid())
  }  // if (use_mmap)
  if (bytes_read == 0)
    PERFETTO_LOG("Cannot use mmap on this system. Falling back on read()");
#endif  // PERFETTO_HAS_MMAP()
  if (bytes_read == 0) {
    base::ScopedFile fd(base::OpenFile(filename, O_RDONLY));
    if (!fd) {
      return base::ErrStatus("could not open trace file %s (errno: %d, %s)",
                             filename, errno, strerror(errno));
    }
    RETURN_IF_ERROR(
        ReadTraceUsingRead(tp, *fd, &bytes_read, progress_callback));
  }
  tp->SetCurrentTraceName(filename);

  if (progress_callback)
    progress_callback(bytes_read);
  return base::OkStatus();
}
}  // namespace perfetto::trace_processor
