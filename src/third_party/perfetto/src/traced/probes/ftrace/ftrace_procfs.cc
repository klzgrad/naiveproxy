/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/traced/probes/ftrace/ftrace_procfs.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto {

// Reading /trace produces human readable trace output.
// Writing to this file clears all trace buffers for all CPUS.

// Writing to /trace_marker file injects an event into the trace buffer.

// Reading /tracing_on returns 1/0 if tracing is enabled/disabled.
// Writing 1/0 to this file enables/disables tracing.
// Disabling tracing with this file prevents further writes but
// does not clear the buffer.

namespace {

namespace {
constexpr char kRssStatThrottledTrigger[] =
    "hist:keys=mm_id,member:bucket=size/0x80000"
    ":onchange($bucket).rss_stat_throttled(mm_id,curr,member,size)";

// Kernel tracepoints |syscore_resume| and |timekeeping_freeze| are mutually
// exclusive: for any given suspend, one event (but not both) will be emitted
// depending on whether it is |S2RAM| vs |S2idle| codepath respectively.
constexpr char kSuspendResumeMinimalTrigger[] =
    "hist:keys=start:size=128:onmatch(power.suspend_resume)"
    ".trace(suspend_resume_minimal, start) if (action == 'syscore_resume')"
    "||(action == 'timekeeping_freeze')";
}  // namespace

void KernelLogWrite(const char* s) {
  PERFETTO_DCHECK(*s && s[strlen(s) - 1] == '\n');
  if (FtraceProcfs::g_kmesg_fd != -1)
    base::ignore_result(base::WriteAll(FtraceProcfs::g_kmesg_fd, s, strlen(s)));
}

bool WriteFileInternal(const std::string& path,
                       const std::string& str,
                       int flags) {
  base::ScopedFile fd = base::OpenFile(path, flags);
  if (!fd)
    return false;
  ssize_t written = base::WriteAll(fd.get(), str.c_str(), str.length());
  ssize_t length = static_cast<ssize_t>(str.length());
  // This should either fail or write fully.
  PERFETTO_CHECK(written == length || written == -1);
  return written == length;
}

}  // namespace

// static
int FtraceProcfs::g_kmesg_fd = -1;  // Set by ProbesMain() in probes.cc .

const char* const FtraceProcfs::kTracingPaths[] = {
    "/sys/kernel/tracing/",
    "/sys/kernel/debug/tracing/",
    nullptr,
};

// static
std::unique_ptr<FtraceProcfs> FtraceProcfs::CreateGuessingMountPoint(
    const std::string& instance_path) {
  std::unique_ptr<FtraceProcfs> ftrace_procfs;
  size_t index = 0;
  while (!ftrace_procfs && kTracingPaths[index]) {
    std::string path = kTracingPaths[index++];
    if (!instance_path.empty())
      path += instance_path;

    ftrace_procfs = Create(path);
  }
  return ftrace_procfs;
}

// static
std::unique_ptr<FtraceProcfs> FtraceProcfs::Create(const std::string& root) {
  if (!CheckRootPath(root))
    return nullptr;
  return std::unique_ptr<FtraceProcfs>(new FtraceProcfs(root));
}

FtraceProcfs::FtraceProcfs(const std::string& root) : root_(root) {}
FtraceProcfs::~FtraceProcfs() = default;

bool FtraceProcfs::SetSyscallFilter(const std::set<size_t>& filter) {
  std::vector<std::string> parts;
  for (size_t id : filter) {
    base::StackString<16> m("id == %zu", id);
    parts.push_back(m.ToStdString());
  }

  std::string filter_str = "0";
  if (!parts.empty()) {
    filter_str = base::Join(parts, " || ");
  }

  for (const char* event : {"sys_enter", "sys_exit"}) {
    std::string path = root_ + "events/raw_syscalls/" + event + "/filter";
    if (!WriteToFile(path, filter_str)) {
      PERFETTO_ELOG("Failed to write file: %s", path.c_str());
      return false;
    }
  }
  return true;
}

bool FtraceProcfs::EnableEvent(const std::string& group,
                               const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/enable";

  // Create any required triggers for the ftrace event being enabled.
  // Some ftrace events (synthetic events) need to set up an event trigger
  MaybeSetUpEventTriggers(group, name);

  if (WriteToFile(path, "1"))
    return true;
  path = root_ + "set_event";
  return AppendToFile(path, group + ":" + name);
}

bool FtraceProcfs::CreateKprobeEvent(const std::string& group,
                                     const std::string& name,
                                     bool is_retprobe) {
  std::string path = root_ + "kprobe_events";
  std::string probe =
      (is_retprobe ? std::string("r") + std::string(kKretprobeDefaultMaxactives)
                   : "p") +
      std::string(":") + group + "/" + name + " " + name;

  PERFETTO_DLOG("Writing \"%s >> %s\"", probe.c_str(), path.c_str());

  bool ret = AppendToFile(path, probe);
  if (!ret) {
    if (errno == EEXIST) {
      // The kprobe event defined by group/name already exists.
      // TODO maybe because the /sys/kernel/tracing/kprobe_events file has not
      // been properly cleaned up after tracing
      PERFETTO_DLOG("Kprobe event %s::%s already exists", group.c_str(),
                    name.c_str());
      return true;
    }
    PERFETTO_PLOG("Failed writing '%s' to '%s'", probe.c_str(), path.c_str());
  }

  return ret;
}

// Utility function to remove kprobe event from the system
bool FtraceProcfs::RemoveKprobeEvent(const std::string& group,
                                     const std::string& name) {
  PERFETTO_DLOG("RemoveKprobeEvent %s::%s", group.c_str(), name.c_str());
  std::string path = root_ + "kprobe_events";
  return AppendToFile(path, "-:" + group + "/" + name);
}

std::string FtraceProcfs::ReadKprobeStats() const {
  std::string path = root_ + "/kprobe_profile";
  return ReadFileIntoString(path);
}

bool FtraceProcfs::DisableEvent(const std::string& group,
                                const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/enable";

  bool ret = WriteToFile(path, "0");
  if (!ret) {
    path = root_ + "set_event";
    ret = AppendToFile(path, "!" + group + ":" + name);
  }

  // Remove any associated event triggers after disabling the event
  MaybeTearDownEventTriggers(group, name);

  return ret;
}

bool FtraceProcfs::IsEventAccessible(const std::string& group,
                                     const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/enable";

  return IsFileWriteable(path);
}

bool FtraceProcfs::IsEventFormatReadable(const std::string& group,
                                         const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/format";

  return IsFileReadable(path);
}

bool FtraceProcfs::DisableAllEvents() {
  std::string path = root_ + "events/enable";
  return WriteToFile(path, "0");
}

bool FtraceProcfs::IsGenericSetEventWritable() {
  std::string path = root_ + "set_event";

  return IsFileWriteable(path);
}

std::string FtraceProcfs::ReadEventFormat(const std::string& group,
                                          const std::string& name) const {
  std::string path = root_ + "events/" + group + "/" + name + "/format";
  return ReadFileIntoString(path);
}

std::string FtraceProcfs::GetCurrentTracer() {
  std::string path = root_ + "current_tracer";
  std::string current_tracer = ReadFileIntoString(path);
  return base::StripSuffix(current_tracer, "\n");
}

bool FtraceProcfs::SetCurrentTracer(const std::string& tracer) {
  std::string path = root_ + "current_tracer";
  return WriteToFile(path, tracer);
}

bool FtraceProcfs::ResetCurrentTracer() {
  return SetCurrentTracer("nop");
}

bool FtraceProcfs::AppendFunctionFilters(
    const std::vector<std::string>& filters) {
  std::string path = root_ + "set_ftrace_filter";
  std::string filter = base::Join(filters, "\n");

  // The same file accepts special actions to perform when a corresponding
  // kernel function is hit (regardless of active tracer). For example
  // "__schedule_bug:traceoff" would disable tracing once __schedule_bug is
  // called.
  // We disallow these commands as most of them break the isolation of
  // concurrent ftrace data sources (as the underlying ftrace instance is
  // shared).
  if (base::Contains(filter, ':')) {
    PERFETTO_ELOG("Filter commands are disallowed.");
    return false;
  }
  return AppendToFile(path, filter);
}

bool FtraceProcfs::ClearFunctionFilters() {
  std::string path = root_ + "set_ftrace_filter";
  return ClearFile(path);
}

bool FtraceProcfs::SetMaxGraphDepth(uint32_t depth) {
  std::string path = root_ + "max_graph_depth";
  return WriteNumberToFile(path, depth);
}

bool FtraceProcfs::ClearMaxGraphDepth() {
  std::string path = root_ + "max_graph_depth";
  return WriteNumberToFile(path, 0);
}

bool FtraceProcfs::AppendFunctionGraphFilters(
    const std::vector<std::string>& filters) {
  std::string path = root_ + "set_graph_function";
  std::string filter = base::Join(filters, "\n");
  return AppendToFile(path, filter);
}

bool FtraceProcfs::ClearFunctionGraphFilters() {
  std::string path = root_ + "set_graph_function";
  return ClearFile(path);
}

std::vector<std::string> FtraceProcfs::ReadEventTriggers(
    const std::string& group,
    const std::string& name) const {
  std::string path = root_ + "events/" + group + "/" + name + "/trigger";
  std::string s = ReadFileIntoString(path);
  std::vector<std::string> triggers;

  for (base::StringSplitter ss(s, '\n'); ss.Next();) {
    std::string trigger = ss.cur_token();
    if (trigger.empty() || trigger[0] == '#')
      continue;

    base::StringSplitter ts(trigger, ' ');
    PERFETTO_CHECK(ts.Next());
    triggers.push_back(ts.cur_token());
  }

  return triggers;
}

bool FtraceProcfs::CreateEventTrigger(const std::string& group,
                                      const std::string& name,
                                      const std::string& trigger) {
  std::string path = root_ + "events/" + group + "/" + name + "/trigger";
  return WriteToFile(path, trigger);
}

bool FtraceProcfs::RemoveEventTrigger(const std::string& group,
                                      const std::string& name,
                                      const std::string& trigger) {
  std::string path = root_ + "events/" + group + "/" + name + "/trigger";
  return WriteToFile(path, "!" + trigger);
}

bool FtraceProcfs::RemoveAllEventTriggers(const std::string& group,
                                          const std::string& name) {
  std::vector<std::string> triggers = ReadEventTriggers(group, name);

  // Remove the triggers in reverse order since a trigger can depend
  // on another trigger created earlier.
  for (auto it = triggers.rbegin(); it != triggers.rend(); ++it)
    if (!RemoveEventTrigger(group, name, *it))
      return false;
  return true;
}

bool FtraceProcfs::MaybeSetUpEventTriggers(const std::string& group,
                                           const std::string& name) {
  bool ret = true;

  if (group == "synthetic") {
    if (name == "rss_stat_throttled") {
      ret = RemoveAllEventTriggers("kmem", "rss_stat") &&
            CreateEventTrigger("kmem", "rss_stat", kRssStatThrottledTrigger);
    } else if (name == "suspend_resume_minimal") {
      ret = RemoveAllEventTriggers("power", "suspend_resume") &&
            CreateEventTrigger("power", "suspend_resume",
                               kSuspendResumeMinimalTrigger);
    }
  }

  if (!ret) {
    PERFETTO_PLOG("Failed to setup event triggers for %s:%s", group.c_str(),
                  name.c_str());
  }

  return ret;
}

bool FtraceProcfs::MaybeTearDownEventTriggers(const std::string& group,
                                              const std::string& name) {
  bool ret = true;

  if (group == "synthetic") {
    if (name == "rss_stat_throttled") {
      ret = RemoveAllEventTriggers("kmem", "rss_stat");
    } else if (name == "suspend_resume_minimal") {
      ret = RemoveEventTrigger("power", "suspend_resume",
                               kSuspendResumeMinimalTrigger);
    }
  }

  if (!ret) {
    PERFETTO_PLOG("Failed to tear down event triggers for: %s:%s",
                  group.c_str(), name.c_str());
  }

  return ret;
}

bool FtraceProcfs::SupportsRssStatThrottled() {
  std::string group = "synthetic";
  std::string name = "rss_stat_throttled";

  // Check if the trigger already exists. Don't try recreating
  // or removing the trigger if it is already in use.
  auto triggers = ReadEventTriggers("kmem", "rss_stat");
  for (const auto& trigger : triggers) {
    // The kernel shows all the default values of a trigger
    // when read from and trace event 'trigger' file.
    //
    // Trying to match the complete trigger string is prone
    // to fail if, in the future, the kernel changes default
    // fields or values for event triggers.
    //
    // Do a partial match on the generated event name
    // (rss_stat_throttled) to detect if the trigger
    // is already created.
    if (trigger.find(name) != std::string::npos)
      return true;
  }

  // Attempt to create rss_stat_throttled hist trigger */
  bool ret = MaybeSetUpEventTriggers(group, name);

  return ret && MaybeTearDownEventTriggers(group, name);
}

std::string FtraceProcfs::ReadPrintkFormats() const {
  std::string path = root_ + "printk_formats";
  return ReadFileIntoString(path);
}

std::vector<std::string> FtraceProcfs::ReadEnabledEvents() {
  std::string path = root_ + "set_event";
  std::string s = ReadFileIntoString(path);
  base::StringSplitter ss(s, '\n');
  std::vector<std::string> events;
  while (ss.Next()) {
    std::string event = ss.cur_token();
    if (event.empty())
      continue;
    events.push_back(base::StripChars(event, ":", '/'));
  }
  return events;
}

std::string FtraceProcfs::ReadPageHeaderFormat() const {
  std::string path = root_ + "events/header_page";
  return ReadFileIntoString(path);
}

base::ScopedFile FtraceProcfs::OpenCpuStats(size_t cpu) const {
  std::string path = root_ + "per_cpu/cpu" + std::to_string(cpu) + "/stats";
  return base::OpenFile(path, O_RDONLY);
}

std::string FtraceProcfs::ReadCpuStats(size_t cpu) const {
  std::string path = root_ + "per_cpu/cpu" + std::to_string(cpu) + "/stats";
  return ReadFileIntoString(path);
}

size_t FtraceProcfs::NumberOfCpus() const {
  static size_t num_cpus = static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
  return num_cpus;
}

void FtraceProcfs::ClearTrace() {
  std::string path = root_ + "trace";
  PERFETTO_CHECK(ClearFile(path));  // Could not clear.

  // Truncating the trace file leads to tracing_reset_online_cpus being called
  // in the kernel.
  //
  // In case some of the CPUs were not online, their buffer needs to be
  // cleared manually.
  //
  // We cannot use PERFETTO_CHECK as we might get a permission denied error
  // on Android. The permissions to these files are configured in
  // platform/framework/native/cmds/atrace/atrace.rc.
  for (size_t cpu = 0, num_cpus = NumberOfCpus(); cpu < num_cpus; cpu++) {
    ClearPerCpuTrace(cpu);
  }
}

void FtraceProcfs::ClearPerCpuTrace(size_t cpu) {
  if (!ClearFile(root_ + "per_cpu/cpu" + std::to_string(cpu) + "/trace"))
    PERFETTO_ELOG("Failed to clear buffer for CPU %zu", cpu);
}

bool FtraceProcfs::WriteTraceMarker(const std::string& str) {
  std::string path = root_ + "trace_marker";
  return WriteToFile(path, str);
}

bool FtraceProcfs::SetCpuBufferSizeInPages(size_t pages) {
  std::string path = root_ + "buffer_size_kb";
  return WriteNumberToFile(path, pages * (base::GetSysPageSize() / 1024ul));
}

// This returns the rounded up pages of the cpu buffer size.
// In case of any error, this returns 1.
size_t FtraceProcfs::GetCpuBufferSizeInPages() {
  std::string path = root_ + "buffer_size_kb";
  auto str = ReadFileIntoString(path);

  if (str.size() == 0) {
    PERFETTO_ELOG("Failed to read per-cpu buffer size.");
    return 1;
  }

  // For the root instance, before starting tracing, the buffer_size_kb
  // returns something like "7 (expanded: 1408)". We also cut off the
  // last newline('\n').
  std::size_t found = str.find_first_not_of("0123456789");
  if (found != std::string::npos) {
    str.resize(found);
  }

  uint32_t page_in_kb = base::GetSysPageSize() / 1024ul;
  std::optional<uint32_t> size_kb = base::StringToUInt32(str);
  return (size_kb.value_or(1) + page_in_kb - 1) / page_in_kb;
}

bool FtraceProcfs::GetTracingOn() {
  std::string path = root_ + "tracing_on";
  char tracing_on = ReadOneCharFromFile(path);
  if (tracing_on == '\0')
    PERFETTO_PLOG("Failed to read %s", path.c_str());
  return tracing_on == '1';
}

bool FtraceProcfs::SetTracingOn(bool on) {
  std::string path = root_ + "tracing_on";
  if (!WriteToFile(path, on ? "1" : "0")) {
    PERFETTO_PLOG("Failed to write %s", path.c_str());
    return false;
  }
  if (on) {
    KernelLogWrite("perfetto: enabled ftrace\n");
    PERFETTO_LOG("enabled ftrace in %s", root_.c_str());
  } else {
    KernelLogWrite("perfetto: disabled ftrace\n");
    PERFETTO_LOG("disabled ftrace in %s", root_.c_str());
  }

  return true;
}

bool FtraceProcfs::IsTracingAvailable() {
  std::string current_tracer = GetCurrentTracer();

  // Ftrace tracing is available if current_tracer == "nop".
  // events/enable could be 0, 1, X or 0*. 0* means events would be
  // dynamically enabled so we need to treat as event tracing is in use.
  // However based on the discussion in asop/2328817, on Android events/enable
  // is "X" after boot up. To avoid causing more problem, the decision is just
  // look at current_tracer.
  // As the discussion in asop/2328817, if GetCurrentTracer failed to
  // read file and return "", we treat it as tracing is available.
  return current_tracer == "nop" || current_tracer == "";
}

bool FtraceProcfs::SetClock(const std::string& clock_name) {
  std::string path = root_ + "trace_clock";
  return WriteToFile(path, clock_name);
}

std::string FtraceProcfs::GetClock() {
  std::string path = root_ + "trace_clock";
  std::string s = ReadFileIntoString(path);

  size_t start = s.find('[');
  if (start == std::string::npos)
    return "";

  size_t end = s.find(']', start);
  if (end == std::string::npos)
    return "";

  return s.substr(start + 1, end - start - 1);
}

std::set<std::string> FtraceProcfs::AvailableClocks() {
  std::string path = root_ + "trace_clock";
  std::string s = ReadFileIntoString(path);
  std::set<std::string> names;

  size_t start = 0;
  size_t end = 0;

  for (;;) {
    end = s.find(' ', start);
    if (end == std::string::npos)
      end = s.size();
    while (end > start && s[end - 1] == '\n')
      end--;
    if (start == end)
      break;

    std::string name = s.substr(start, end - start);

    if (name[0] == '[')
      name = name.substr(1, name.size() - 2);

    names.insert(name);

    if (end == s.size())
      break;

    start = end + 1;
  }

  return names;
}

uint32_t FtraceProcfs::ReadBufferPercent() {
  std::string path = root_ + "buffer_percent";
  std::string raw = ReadFileIntoString(path);
  std::optional<uint32_t> percent =
      base::StringToUInt32(base::StripSuffix(raw, "\n"));
  return percent.has_value() ? *percent : 0;
}

bool FtraceProcfs::SetBufferPercent(uint32_t percent) {
  std::string path = root_ + "buffer_percent";
  return WriteNumberToFile(path, percent);
}

bool FtraceProcfs::WriteNumberToFile(const std::string& path, size_t value) {
  // 2^65 requires 20 digits to write.
  char buf[21];
  snprintf(buf, sizeof(buf), "%zu", value);
  return WriteToFile(path, std::string(buf));
}

bool FtraceProcfs::WriteToFile(const std::string& path,
                               const std::string& str) {
  return WriteFileInternal(path, str, O_WRONLY);
}

bool FtraceProcfs::AppendToFile(const std::string& path,
                                const std::string& str) {
  return WriteFileInternal(path, str, O_WRONLY | O_APPEND);
}

base::ScopedFile FtraceProcfs::OpenPipeForCpu(size_t cpu) {
  std::string path =
      root_ + "per_cpu/cpu" + std::to_string(cpu) + "/trace_pipe_raw";
  return base::OpenFile(path, O_RDONLY | O_NONBLOCK);
}

char FtraceProcfs::ReadOneCharFromFile(const std::string& path) {
  base::ScopedFile fd = base::OpenFile(path, O_RDONLY);
  PERFETTO_CHECK(fd);
  char result = '\0';
  ssize_t bytes = PERFETTO_EINTR(read(fd.get(), &result, 1));
  PERFETTO_CHECK(bytes == 1 || bytes == -1);
  return result;
}

bool FtraceProcfs::ClearFile(const std::string& path) {
  base::ScopedFile fd = base::OpenFile(path, O_WRONLY | O_TRUNC);
  return !!fd;
}

bool FtraceProcfs::IsFileWriteable(const std::string& path) {
  return access(path.c_str(), W_OK) == 0;
}

bool FtraceProcfs::IsFileReadable(const std::string& path) {
  return access(path.c_str(), R_OK) == 0;
}

std::string FtraceProcfs::ReadFileIntoString(const std::string& path) const {
  // You can't seek or stat the procfs files on Android.
  // The vast majority (884/886) of format files are under 4k.
  std::string str;
  str.reserve(4096);
  if (!base::ReadFile(path, &str))
    return "";
  return str;
}

const std::set<std::string> FtraceProcfs::GetEventNamesForGroup(
    const std::string& path) const {
  std::set<std::string> names;
  std::string full_path = root_ + path;
  base::ScopedDir dir(opendir(full_path.c_str()));
  if (!dir) {
    PERFETTO_DLOG("Unable to read events from %s", full_path.c_str());
    return names;
  }
  struct dirent* ent;
  while ((ent = readdir(*dir)) != nullptr) {
    if (strncmp(ent->d_name, ".", 1) == 0 ||
        strncmp(ent->d_name, "..", 2) == 0) {
      continue;
    }
    // Check ent is a directory.
    struct stat statbuf;
    std::string dir_path = full_path + "/" + ent->d_name;
    if (stat(dir_path.c_str(), &statbuf) == 0) {
      if (S_ISDIR(statbuf.st_mode)) {
        names.insert(ent->d_name);
      }
    }
  }
  return names;
}

uint32_t FtraceProcfs::ReadEventId(const std::string& group,
                                   const std::string& name) const {
  std::string path = root_ + "events/" + group + "/" + name + "/id";

  std::string str;
  if (!base::ReadFile(path, &str))
    return 0;

  if (str.size() && str[str.size() - 1] == '\n')
    str.resize(str.size() - 1);

  std::optional<uint32_t> id = base::StringToUInt32(str);
  if (!id)
    return 0;
  return *id;
}

// static
bool FtraceProcfs::CheckRootPath(const std::string& root) {
  base::ScopedFile fd = base::OpenFile(root + "trace", O_RDONLY);
  return static_cast<bool>(fd);
}

}  // namespace perfetto
