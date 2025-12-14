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

#include "src/traced/probes/ftrace/tracefs.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/flags.h"
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
  if (Tracefs::g_kmesg_fd != -1)
    base::ignore_result(base::WriteAll(Tracefs::g_kmesg_fd, s, strlen(s)));
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
int Tracefs::g_kmesg_fd = -1;  // Set by ProbesMain() in probes.cc .

const char* const Tracefs::kTracingPaths[] = {
    "/sys/kernel/tracing/",
    "/sys/kernel/debug/tracing/",
    nullptr,
};

// static
std::unique_ptr<Tracefs> Tracefs::CreateGuessingMountPoint(
    const std::string& instance_path) {
  std::unique_ptr<Tracefs> tracefs;
  size_t index = 0;
  while (!tracefs && kTracingPaths[index]) {
    std::string path = kTracingPaths[index++];
    if (!instance_path.empty())
      path += instance_path;

    tracefs = Create(path);
  }
  return tracefs;
}

// static
std::unique_ptr<Tracefs> Tracefs::Create(const std::string& root) {
  if (!CheckRootPath(root))
    return nullptr;
  return std::unique_ptr<Tracefs>(new Tracefs(root));
}

Tracefs::Tracefs(const std::string& root) : root_(root) {}
Tracefs::~Tracefs() = default;

bool Tracefs::SetSyscallFilter(const std::set<size_t>& filter) {
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

bool Tracefs::EnableEvent(const std::string& group, const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/enable";

  // Create any required triggers for the ftrace event being enabled.
  // Some ftrace events (synthetic events) need to set up an event trigger
  MaybeSetUpEventTriggers(group, name);

  if (WriteToFile(path, "1"))
    return true;
  path = root_ + "set_event";
  return AppendToFile(path, group + ":" + name);
}

bool Tracefs::CreateKprobeEvent(const std::string& group,
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
bool Tracefs::RemoveKprobeEvent(const std::string& group,
                                const std::string& name) {
  PERFETTO_DLOG("RemoveKprobeEvent %s::%s", group.c_str(), name.c_str());
  std::string path = root_ + "kprobe_events";
  return AppendToFile(path, "-:" + group + "/" + name);
}

std::string Tracefs::ReadKprobeStats() const {
  std::string path = root_ + "/kprobe_profile";
  return ReadFileIntoString(path);
}

bool Tracefs::DisableEvent(const std::string& group, const std::string& name) {
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

bool Tracefs::IsEventAccessible(const std::string& group,
                                const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/enable";

  return IsFileWriteable(path);
}

bool Tracefs::IsEventFormatReadable(const std::string& group,
                                    const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/format";

  return IsFileReadable(path);
}

bool Tracefs::DisableAllEvents() {
  std::string path = root_ + "events/enable";
  return WriteToFile(path, "0");
}

bool Tracefs::IsGenericSetEventWritable() {
  std::string path = root_ + "set_event";

  return IsFileWriteable(path);
}

std::string Tracefs::ReadEventFormat(const std::string& group,
                                     const std::string& name) const {
  std::string path = root_ + "events/" + group + "/" + name + "/format";
  return ReadFileIntoString(path);
}

std::string Tracefs::GetCurrentTracer() {
  std::string path = root_ + "current_tracer";
  std::string current_tracer = ReadFileIntoString(path);
  return base::StripSuffix(current_tracer, "\n");
}

bool Tracefs::SetCurrentTracer(const std::string& tracer) {
  std::string path = root_ + "current_tracer";
  return WriteToFile(path, tracer);
}

bool Tracefs::ResetCurrentTracer() {
  return SetCurrentTracer("nop");
}

bool Tracefs::AppendFunctionFilters(const std::vector<std::string>& filters) {
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

bool Tracefs::ClearFunctionFilters() {
  std::string path = root_ + "set_ftrace_filter";
  return ClearFile(path);
}

bool Tracefs::SetMaxGraphDepth(uint32_t depth) {
  std::string path = root_ + "max_graph_depth";
  return WriteNumberToFile(path, depth);
}

bool Tracefs::ClearMaxGraphDepth() {
  std::string path = root_ + "max_graph_depth";
  return WriteNumberToFile(path, 0);
}

bool Tracefs::SetEventTidFilter(const std::vector<std::string>& tids_to_trace) {
  std::string path = root_ + "set_event_pid";
  std::string filter = base::Join(tids_to_trace, " ");
  return WriteToFile(path, filter);
}

bool Tracefs::ClearEventTidFilter() {
  std::string path = root_ + "set_event_pid";
  return ClearFile(path);
}

std::optional<bool> Tracefs::GetTracefsOption(const std::string& option) {
  std::string path = root_ + "options/" + option;
  std::string value = base::TrimWhitespace(ReadFileIntoString(path));
  if (value != "0" && value != "1") {
    return std::nullopt;
  }
  return value == "1";
}

bool Tracefs::SetTracefsOption(const std::string& option, bool enabled) {
  std::string path = root_ + "options/" + option;
  return WriteToFile(path, enabled ? "1" : "0");
}

std::optional<std::string> Tracefs::GetTracingCpuMask() {
  std::string path = root_ + "tracing_cpumask";
  std::string cpumask = base::TrimWhitespace(ReadFileIntoString(path));
  if (cpumask.empty()) {
    return std::nullopt;
  }
  return cpumask;
}

bool Tracefs::SetTracingCpuMask(const std::string& cpumask) {
  std::string path = root_ + "tracing_cpumask";
  return WriteToFile(path, cpumask);
}

bool Tracefs::AppendFunctionGraphFilters(
    const std::vector<std::string>& filters) {
  std::string path = root_ + "set_graph_function";
  std::string filter = base::Join(filters, "\n");
  return AppendToFile(path, filter);
}

bool Tracefs::ClearFunctionGraphFilters() {
  std::string path = root_ + "set_graph_function";
  return ClearFile(path);
}

std::vector<std::string> Tracefs::ReadEventTriggers(
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

bool Tracefs::CreateEventTrigger(const std::string& group,
                                 const std::string& name,
                                 const std::string& trigger) {
  std::string path = root_ + "events/" + group + "/" + name + "/trigger";
  return WriteToFile(path, trigger);
}

bool Tracefs::RemoveEventTrigger(const std::string& group,
                                 const std::string& name,
                                 const std::string& trigger) {
  std::string path = root_ + "events/" + group + "/" + name + "/trigger";
  return WriteToFile(path, "!" + trigger);
}

bool Tracefs::RemoveAllEventTriggers(const std::string& group,
                                     const std::string& name) {
  std::vector<std::string> triggers = ReadEventTriggers(group, name);

  // Remove the triggers in reverse order since a trigger can depend
  // on another trigger created earlier.
  for (auto it = triggers.rbegin(); it != triggers.rend(); ++it)
    if (!RemoveEventTrigger(group, name, *it))
      return false;
  return true;
}

bool Tracefs::MaybeSetUpEventTriggers(const std::string& group,
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

bool Tracefs::MaybeTearDownEventTriggers(const std::string& group,
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

bool Tracefs::SupportsRssStatThrottled() {
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

std::string Tracefs::ReadPrintkFormats() const {
  std::string path = root_ + "printk_formats";
  return ReadFileIntoString(path);
}

std::vector<std::string> Tracefs::ReadEnabledEvents() {
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

std::string Tracefs::ReadPageHeaderFormat() const {
  std::string path = root_ + "events/header_page";
  return ReadFileIntoString(path);
}

base::ScopedFile Tracefs::OpenCpuStats(size_t cpu) const {
  std::string path = root_ + "per_cpu/cpu" + std::to_string(cpu) + "/stats";
  return base::OpenFile(path, O_RDONLY);
}

std::string Tracefs::ReadCpuStats(size_t cpu) const {
  std::string path = root_ + "per_cpu/cpu" + std::to_string(cpu) + "/stats";
  return ReadFileIntoString(path);
}

size_t Tracefs::NumberOfCpus() const {
  static size_t num_cpus = static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
  return num_cpus;
}

size_t Tracefs::NumberOfOnlineCpus() const {
  return static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
}

std::optional<std::vector<uint32_t>> Tracefs::GetOfflineCpus() const {
  std::string offline_cpus_str;
  if (!ReadFile("/sys/devices/system/cpu/offline", &offline_cpus_str)) {
    PERFETTO_ELOG("Failed to read offline cpus file");
    return std::nullopt;
  }
  offline_cpus_str = base::TrimWhitespace(offline_cpus_str);

  // The offline cpus file contains a list of comma-separated CPU ranges.
  // Each range is either a single CPU or a range of CPUs, e.g. "0-3,5,7-9".
  // Source: https://docs.kernel.org/admin-guide/cputopology.html
  std::vector<uint32_t> offline_cpus;
  for (base::StringSplitter ss(offline_cpus_str, ','); ss.Next();) {
    base::StringView offline_cpu_range(ss.cur_token(), ss.cur_token_size());
    size_t dash_pos = offline_cpu_range.find('-');
    if (dash_pos == base::StringView::npos) {
      // Single CPU in the format of "%d".
      std::optional<uint32_t> cpu = base::StringViewToUInt32(offline_cpu_range);
      if (cpu.has_value()) {
        offline_cpus.push_back(cpu.value());
      } else {
        PERFETTO_ELOG("Failed to parse single CPU from offline CPU range: %s",
                      offline_cpu_range.data());
        return std::nullopt;
      }
    } else {
      // Range of CPUs in the format of "%d-%d".
      std::optional<uint32_t> start_cpu =
          base::StringViewToUInt32(offline_cpu_range.substr(0, dash_pos));
      std::optional<uint32_t> end_cpu =
          base::StringViewToUInt32(offline_cpu_range.substr(dash_pos + 1));
      if (start_cpu.has_value() && end_cpu.has_value()) {
        for (auto cpu = start_cpu.value(); cpu <= end_cpu.value(); ++cpu) {
          offline_cpus.push_back(cpu);
        }
      } else {
        PERFETTO_ELOG("Failed to parse CPU range from offline CPU range: %s",
                      offline_cpu_range.data());
        return std::nullopt;
      }
    }
  }
  return offline_cpus;
}

void Tracefs::ClearTrace() {
  std::string path = root_ + "trace";
  PERFETTO_CHECK(ClearFile(path));  // Could not clear.

  const auto total_cpu_count = NumberOfCpus();

  if constexpr (base::flags::ftrace_clear_offline_cpus_only) {
    const auto online_cpu_count = NumberOfOnlineCpus();

    // Truncating the trace file leads to tracing_reset_online_cpus being called
    // in the kernel. So if all cpus are online, no further action needed.
    if (total_cpu_count == online_cpu_count)
      return;

    PERFETTO_LOG(
        "Since %zu / %zu CPUS are online, clearing buffer for the offline ones "
        "individually.",
        online_cpu_count, total_cpu_count);

    std::optional<std::vector<uint32_t>> offline_cpus = GetOfflineCpus();

    // We cannot use PERFETTO_CHECK on ClearPerCpuTrace as we might get a
    // permission denied error on Android. The permissions to these files are
    // configured in platform/framework/native/cmds/atrace/atrace.rc.
    if (offline_cpus.has_value()) {
      for (const auto& cpu : offline_cpus.value()) {
        ClearPerCpuTrace(cpu);
      }
      return;
    }
  }

  // If the feature is disabled / we can't determine which CPUs are offline,
  // clear the buffer for all possible CPUs.
  for (size_t cpu = 0; cpu < total_cpu_count; cpu++) {
    ClearPerCpuTrace(cpu);
  }
}

void Tracefs::ClearPerCpuTrace(size_t cpu) {
  if (!ClearFile(root_ + "per_cpu/cpu" + std::to_string(cpu) + "/trace"))
    PERFETTO_ELOG("Failed to clear buffer for CPU %zu", cpu);
}

bool Tracefs::WriteTraceMarker(const std::string& str) {
  std::string path = root_ + "trace_marker";
  return WriteToFile(path, str);
}

bool Tracefs::SetCpuBufferSizeInPages(size_t pages) {
  std::string path = root_ + "buffer_size_kb";
  return WriteNumberToFile(path, pages * (base::GetSysPageSize() / 1024ul));
}

// This returns the rounded up pages of the cpu buffer size.
// In case of any error, this returns 1.
size_t Tracefs::GetCpuBufferSizeInPages() {
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

bool Tracefs::GetTracingOn() {
  std::string path = root_ + "tracing_on";
  char tracing_on = ReadOneCharFromFile(path);
  if (tracing_on == '\0')
    PERFETTO_PLOG("Failed to read %s", path.c_str());
  return tracing_on == '1';
}

bool Tracefs::SetTracingOn(bool on) {
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

bool Tracefs::IsTracingAvailable() {
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

bool Tracefs::SetClock(const std::string& clock_name) {
  std::string path = root_ + "trace_clock";
  return WriteToFile(path, clock_name);
}

std::string Tracefs::GetClock() {
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

std::set<std::string> Tracefs::AvailableClocks() {
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

uint32_t Tracefs::ReadBufferPercent() {
  std::string path = root_ + "buffer_percent";
  std::string raw = ReadFileIntoString(path);
  std::optional<uint32_t> percent =
      base::StringToUInt32(base::StripSuffix(raw, "\n"));
  return percent.has_value() ? *percent : 0;
}

bool Tracefs::SetBufferPercent(uint32_t percent) {
  std::string path = root_ + "buffer_percent";
  return WriteNumberToFile(path, percent);
}

bool Tracefs::WriteNumberToFile(const std::string& path, size_t value) {
  // 2^65 requires 20 digits to write.
  char buf[21];
  snprintf(buf, sizeof(buf), "%zu", value);
  return WriteToFile(path, std::string(buf));
}

bool Tracefs::WriteToFile(const std::string& path, const std::string& str) {
  return WriteFileInternal(path, str, O_WRONLY);
}

bool Tracefs::AppendToFile(const std::string& path, const std::string& str) {
  return WriteFileInternal(path, str, O_WRONLY | O_APPEND);
}

base::ScopedFile Tracefs::OpenPipeForCpu(size_t cpu) {
  std::string path =
      root_ + "per_cpu/cpu" + std::to_string(cpu) + "/trace_pipe_raw";
  return base::OpenFile(path, O_RDONLY | O_NONBLOCK);
}

char Tracefs::ReadOneCharFromFile(const std::string& path) {
  base::ScopedFile fd = base::OpenFile(path, O_RDONLY);
  PERFETTO_CHECK(fd);
  char result = '\0';
  ssize_t bytes = PERFETTO_EINTR(read(fd.get(), &result, 1));
  PERFETTO_CHECK(bytes == 1 || bytes == -1);
  return result;
}

bool Tracefs::ClearFile(const std::string& path) {
  base::ScopedFile fd = base::OpenFile(path, O_WRONLY | O_TRUNC);
  return !!fd;
}

bool Tracefs::IsFileWriteable(const std::string& path) {
  return access(path.c_str(), W_OK) == 0;
}

bool Tracefs::IsFileReadable(const std::string& path) {
  return access(path.c_str(), R_OK) == 0;
}

bool Tracefs::ReadFile(const std::string& path, std::string* str) const {
  return base::ReadFile(path, str);
}

std::string Tracefs::ReadFileIntoString(const std::string& path) const {
  // You can't seek or stat the tracefs files on Android.
  // The vast majority (884/886) of format files are under 4k.
  std::string str;
  str.reserve(4096);
  if (!ReadFile(path, &str))
    return "";
  return str;
}

const std::set<std::string> Tracefs::GetEventNamesForGroup(
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

uint32_t Tracefs::ReadEventId(const std::string& group,
                              const std::string& name) const {
  std::string path = root_ + "events/" + group + "/" + name + "/id";

  std::string str;
  if (!ReadFile(path, &str))
    return 0;

  if (str.size() && str[str.size() - 1] == '\n')
    str.resize(str.size() - 1);

  std::optional<uint32_t> id = base::StringToUInt32(str);
  if (!id)
    return 0;
  return *id;
}

// static
bool Tracefs::CheckRootPath(const std::string& root) {
  base::ScopedFile fd = base::OpenFile(root + "trace", O_RDONLY);
  return static_cast<bool>(fd);
}

}  // namespace perfetto
