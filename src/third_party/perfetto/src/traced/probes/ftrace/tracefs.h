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

#ifndef SRC_TRACED_PROBES_FTRACE_TRACEFS_H_
#define SRC_TRACED_PROBES_FTRACE_TRACEFS_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "perfetto/ext/base/scoped_file.h"

namespace perfetto {

constexpr std::string_view kKretprobeDefaultMaxactives = "1024";

class Tracefs {
 public:
  static const char* const kTracingPaths[];

  // Tries creating an |Tracefs| at the standard tracefs mount points.
  // Takes an optional |instance_path| such as "instances/wifi/", in which case
  // the returned object will be for that ftrace instance path.
  static std::unique_ptr<Tracefs> CreateGuessingMountPoint(
      const std::string& instance_path = "");

  static std::unique_ptr<Tracefs> Create(const std::string& root);

  static int g_kmesg_fd;

  explicit Tracefs(const std::string& root);
  virtual ~Tracefs();

  // Set the filter for syscall events. If empty, clear the filter.
  bool SetSyscallFilter(const std::set<size_t>& filter);

  // Enable the event under with the given |group| and |name|.
  bool EnableEvent(const std::string& group, const std::string& name);

  // Create the kprobe event for the function |name|. The event will be in
  // |group|/|name|. Depending on the value of |is_retprobe|, installs a kprobe
  // or a kretprobe.
  bool CreateKprobeEvent(const std::string& group,
                         const std::string& name,
                         bool is_retprobe);

  // Remove kprobe event from the system
  bool RemoveKprobeEvent(const std::string& group, const std::string& name);

  // Read the "kprobe_profile" file.
  std::string ReadKprobeStats() const;

  // Disable the event under with the given |group| and |name|.
  bool DisableEvent(const std::string& group, const std::string& name);

  // Returns true if the event under the given |group| and |name| exists and its
  // enable file is writeable.
  bool IsEventAccessible(const std::string& group, const std::string& name);

  // Returns true if the event under the given |group| and |name| exists and its
  // format is readable.
  bool IsEventFormatReadable(const std::string& group, const std::string& name);

  // Disable all events by writing to the global enable file.
  bool DisableAllEvents();

  // Returns true if the generic "set_event" interface (that can be used as a
  // falback by EnableEvent) is writable.
  bool IsGenericSetEventWritable();

  // Read the format for event with the given |group| and |name|.
  // virtual for testing.
  virtual std::string ReadEventFormat(const std::string& group,
                                      const std::string& name) const;

  virtual std::string ReadPageHeaderFormat() const;

  std::string GetCurrentTracer();
  // Sets the "current_tracer". Might fail with EBUSY if tracing pipes have
  // already been opened for reading.
  bool SetCurrentTracer(const std::string& tracer);
  // Resets the "current_tracer" to "nop".
  bool ResetCurrentTracer();
  bool AppendFunctionFilters(const std::vector<std::string>& filters);
  bool ClearFunctionFilters();
  bool AppendFunctionGraphFilters(const std::vector<std::string>& filters);
  bool ClearFunctionGraphFilters();
  bool SetMaxGraphDepth(uint32_t depth);
  bool ClearMaxGraphDepth();
  bool SetEventTidFilter(const std::vector<std::string>& tids_to_trace);
  bool ClearEventTidFilter();
  std::optional<bool> GetTracefsOption(const std::string& option);
  bool SetTracefsOption(const std::string& option, bool enabled);
  std::optional<std::string> GetTracingCpuMask();
  bool SetTracingCpuMask(const std::string& cpumask);

  // Get all triggers for event with the given |group| and |name|.
  std::vector<std::string> ReadEventTriggers(const std::string& group,
                                             const std::string& name) const;

  // Create an event trigger for the given |group| and |name|.
  bool CreateEventTrigger(const std::string& group,
                          const std::string& name,
                          const std::string& trigger);

  // Remove an event trigger for the given |group| and |name|.
  bool RemoveEventTrigger(const std::string& group,
                          const std::string& name,
                          const std::string& trigger);

  // Remove all event trigger for the given |group| and |name|.
  bool RemoveAllEventTriggers(const std::string& group,
                              const std::string& name);

  // Sets up any associated event trigger before enabling the event
  bool MaybeSetUpEventTriggers(const std::string& group,
                               const std::string& name);

  // Tears down any associated event trigger after disabling the event
  bool MaybeTearDownEventTriggers(const std::string& group,
                                  const std::string& name);

  // Returns true if rss_stat_throttled synthetic event is supported
  bool SupportsRssStatThrottled();

  // Read the printk formats file.
  std::string ReadPrintkFormats() const;

  // Opens the "/per_cpu/cpuXX/stats" file for the given |cpu|.
  base::ScopedFile OpenCpuStats(size_t cpu) const;

  // Read the "/per_cpu/cpuXX/stats" file for the given |cpu|.
  std::string ReadCpuStats(size_t cpu) const;

  // Set ftrace buffer size in pages.
  // This size is *per cpu* so for the total size you have to multiply
  // by the number of CPUs.
  bool SetCpuBufferSizeInPages(size_t pages);

  // Returns the current per-cpu buffer size in pages.
  size_t GetCpuBufferSizeInPages();

  // Returns the number of CPUs.
  // This will match the number of tracing/per_cpu/cpuXX directories.
  size_t virtual NumberOfCpus() const;

  // Clears the trace buffers for all CPUs. Blocks until this is done.
  void ClearTrace();

  // Clears the trace buffer for cpu. Blocks until this is done.
  void ClearPerCpuTrace(size_t cpu);

  // Writes the string |str| as an event into the trace buffer.
  bool WriteTraceMarker(const std::string& str);

  // Read tracing_on and return true if tracing_on is 1, otherwise return false.
  bool GetTracingOn();

  // Write 1 to tracing_on if |on| is true, otherwise write 0.
  bool SetTracingOn(bool on);

  // Returns true if ftrace tracing is available.
  // Ftrace tracing is available iff "/current_tracer" is "nop", indicates
  // function tracing is not in use. Necessarily
  // racy: another program could enable/disable tracing at any point.
  bool IsTracingAvailable();

  // Set the clock. |clock_name| should be one of the names returned by
  // AvailableClocks. Setting the clock clears the buffer.
  bool SetClock(const std::string& clock_name);

  // Get the currently set clock.
  std::string GetClock();

  // Get all the available clocks.
  std::set<std::string> AvailableClocks();

  uint32_t ReadBufferPercent();
  bool SetBufferPercent(uint32_t percent);

  // Get all the enabled events.
  virtual std::vector<std::string> ReadEnabledEvents();

  // Open the raw pipe for |cpu|.
  virtual base::ScopedFile OpenPipeForCpu(size_t cpu);

  virtual const std::set<std::string> GetEventNamesForGroup(
      const std::string& path) const;

  // Returns the |id| for event with the given |group| and |name|. Returns 0 if
  // the event doesn't exist, or its /id file could not be read. Not typically
  // needed if already parsing the format file.
  uint32_t ReadEventId(const std::string& group, const std::string& name) const;

  std::string GetRootPath() const { return root_; }

 protected:
  // virtual and protected for testing.
  virtual bool WriteToFile(const std::string& path, const std::string& str);
  virtual bool AppendToFile(const std::string& path, const std::string& str);
  virtual bool ClearFile(const std::string& path);
  virtual bool IsFileWriteable(const std::string& path);
  virtual bool IsFileReadable(const std::string& path);
  virtual char ReadOneCharFromFile(const std::string& path);
  virtual bool ReadFile(const std::string& path, std::string* str) const;
  virtual std::string ReadFileIntoString(const std::string& path) const;
  virtual size_t NumberOfOnlineCpus() const;
  // Parses the list of offline CPUs from "/sys/devices/system/cpu/offline" and
  // returns them as a vector if successful, or std::nullopt if any failure.
  virtual std::optional<std::vector<uint32_t>> GetOfflineCpus() const;

 private:
  // Checks the trace file is present at the given root path.
  static bool CheckRootPath(const std::string& root);

  bool WriteNumberToFile(const std::string& path, size_t value);

  const std::string root_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FTRACE_TRACEFS_H_
