/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/traced/probes/ftrace/predefined_tracepoints.h"

#include <map>

#include "src/traced/probes/ftrace/proto_translation_table.h"
#include "src/traced/probes/ftrace/tracefs.h"

namespace perfetto::predefined_tracepoints {
namespace {
void AddEventGroup(const ProtoTranslationTable* table,
                   const std::string& group,
                   base::FlatSet<GroupAndName>* to) {
  const std::vector<const Event*>* events = table->GetEventsByGroup(group);
  if (!events)
    return;
  for (const Event* event : *events) {
    to->insert(GroupAndName(group, event->name));
  }
}

void PERFETTO_NO_INLINE InsertEvent(const char* group,
                                    const char* name,
                                    base::FlatSet<GroupAndName>* dst) {
  dst->insert(GroupAndName(group, name));
}

base::FlatSet<GroupAndName> GenerateGfxTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "mdss", &events);
  InsertEvent("mdss", "rotator_bw_ao_as_context", &events);
  InsertEvent("mdss", "mdp_trace_counter", &events);
  InsertEvent("mdss", "tracing_mark_write", &events);
  InsertEvent("mdss", "mdp_cmd_wait_pingpong", &events);
  InsertEvent("mdss", "mdp_cmd_kickoff", &events);
  InsertEvent("mdss", "mdp_cmd_release_bw", &events);
  InsertEvent("mdss", "mdp_cmd_readptr_done", &events);
  InsertEvent("mdss", "mdp_cmd_pingpong_done", &events);
  InsertEvent("mdss", "mdp_misr_crc", &events);
  InsertEvent("mdss", "mdp_compare_bw", &events);
  InsertEvent("mdss", "mdp_perf_update_bus", &events);
  InsertEvent("mdss", "mdp_video_underrun_done", &events);
  InsertEvent("mdss", "mdp_commit", &events);
  InsertEvent("mdss", "mdp_mixer_update", &events);
  InsertEvent("mdss", "mdp_perf_prefill_calc", &events);
  InsertEvent("mdss", "mdp_perf_set_ot", &events);
  InsertEvent("mdss", "mdp_perf_set_wm_levels", &events);
  InsertEvent("mdss", "mdp_perf_set_panic_luts", &events);
  InsertEvent("mdss", "mdp_perf_set_qos_luts", &events);
  InsertEvent("mdss", "mdp_sspp_change", &events);
  InsertEvent("mdss", "mdp_sspp_set", &events);
  AddEventGroup(table, "mali", &events);
  InsertEvent("mali", "tracing_mark_write", &events);

  AddEventGroup(table, "sde", &events);
  InsertEvent("sde", "tracing_mark_write", &events);
  InsertEvent("sde", "sde_perf_update_bus", &events);
  InsertEvent("sde", "sde_perf_set_qos_luts", &events);
  InsertEvent("sde", "sde_perf_set_ot", &events);
  InsertEvent("sde", "sde_perf_set_danger_luts", &events);
  InsertEvent("sde", "sde_perf_crtc_update", &events);
  InsertEvent("sde", "sde_perf_calc_crtc", &events);
  InsertEvent("sde", "sde_evtlog", &events);
  InsertEvent("sde", "sde_encoder_underrun", &events);
  InsertEvent("sde", "sde_cmd_release_bw", &events);

  AddEventGroup(table, "dpu", &events);
  InsertEvent("dpu", "tracing_mark_write", &events);
  InsertEvent("dpu", "disp_dpu_underrun", &events);

  AddEventGroup(table, "g2d", &events);
  InsertEvent("g2d", "tracing_mark_write", &events);
  InsertEvent("g2d", "g2d_perf_update_qos", &events);

  AddEventGroup(table, "panel", &events);
  InsertEvent("panel", "panel_write_generic", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateIonTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("kmem", "ion_alloc_buffer_start", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateSchedTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  // Note: sched_wakeup intentionally removed (diverging from atrace), as it
  // is high-volume, but mostly redundant when sched_waking is also enabled.
  // The event can still be enabled explicitly when necessary.
  InsertEvent("sched", "sched_switch", &events);
  InsertEvent("sched", "sched_waking", &events);
  InsertEvent("sched", "sched_blocked_reason", &events);
  InsertEvent("sched", "sched_cpu_hotplug", &events);
  InsertEvent("sched", "sched_pi_setprio", &events);
  InsertEvent("sched", "sched_process_exit", &events);
  AddEventGroup(table, "cgroup", &events);
  InsertEvent("cgroup", "cgroup_transfer_tasks", &events);
  InsertEvent("cgroup", "cgroup_setup_root", &events);
  InsertEvent("cgroup", "cgroup_rmdir", &events);
  InsertEvent("cgroup", "cgroup_rename", &events);
  InsertEvent("cgroup", "cgroup_remount", &events);
  InsertEvent("cgroup", "cgroup_release", &events);
  InsertEvent("cgroup", "cgroup_mkdir", &events);
  InsertEvent("cgroup", "cgroup_destroy_root", &events);
  InsertEvent("cgroup", "cgroup_attach_task", &events);
  InsertEvent("oom", "oom_score_adj_update", &events);
  InsertEvent("task", "task_rename", &events);
  InsertEvent("task", "task_newtask", &events);

  AddEventGroup(table, "systrace", &events);
  InsertEvent("systrace", "0", &events);

  AddEventGroup(table, "scm", &events);
  InsertEvent("scm", "scm_call_start", &events);
  InsertEvent("scm", "scm_call_end", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateIrqTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "irq", &events);
  InsertEvent("irq", "tasklet_hi_exit", &events);
  InsertEvent("irq", "tasklet_hi_entry", &events);
  InsertEvent("irq", "tasklet_exit", &events);
  InsertEvent("irq", "tasklet_entry", &events);
  InsertEvent("irq", "softirq_raise", &events);
  InsertEvent("irq", "softirq_exit", &events);
  InsertEvent("irq", "softirq_entry", &events);
  InsertEvent("irq", "irq_handler_exit", &events);
  InsertEvent("irq", "irq_handler_entry", &events);
  AddEventGroup(table, "ipi", &events);
  InsertEvent("ipi", "ipi_raise", &events);
  InsertEvent("ipi", "ipi_exit", &events);
  InsertEvent("ipi", "ipi_entry", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateIrqOffTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("preemptirq", "irq_enable", &events);
  InsertEvent("preemptirq", "irq_disable", &events);
  return events;
}

base::FlatSet<GroupAndName> GeneratePreemptoffTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("preemptirq", "preempt_enable", &events);
  InsertEvent("preemptirq", "preempt_disable", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateI2cTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "i2c", &events);
  InsertEvent("i2c", "i2c_read", &events);
  InsertEvent("i2c", "i2c_write", &events);
  InsertEvent("i2c", "i2c_result", &events);
  InsertEvent("i2c", "i2c_reply", &events);
  InsertEvent("i2c", "smbus_read", &events);
  InsertEvent("i2c", "smbus_write", &events);
  InsertEvent("i2c", "smbus_result", &events);
  InsertEvent("i2c", "smbus_reply", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateFreqTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  InsertEvent("power", "cpu_frequency", &events);
  InsertEvent("power", "gpu_frequency", &events);
  InsertEvent("power", "clock_set_rate", &events);
  InsertEvent("power", "clock_disable", &events);
  InsertEvent("power", "clock_enable", &events);
  InsertEvent("clk", "clk_set_rate", &events);
  InsertEvent("clk", "clk_disable", &events);
  InsertEvent("clk", "clk_enable", &events);
  InsertEvent("power", "cpu_frequency_limits", &events);
  InsertEvent("power", "suspend_resume", &events);
  InsertEvent("cpuhp", "cpuhp_enter", &events);
  InsertEvent("cpuhp", "cpuhp_exit", &events);
  InsertEvent("cpuhp", "cpuhp_pause", &events);
  AddEventGroup(table, "msm_bus", &events);
  InsertEvent("msm_bus", "bus_update_request_end", &events);
  InsertEvent("msm_bus", "bus_update_request", &events);
  InsertEvent("msm_bus", "bus_rules_matches", &events);
  InsertEvent("msm_bus", "bus_max_votes", &events);
  InsertEvent("msm_bus", "bus_client_status", &events);
  InsertEvent("msm_bus", "bus_bke_params", &events);
  InsertEvent("msm_bus", "bus_bimc_config_limiter", &events);
  InsertEvent("msm_bus", "bus_avail_bw", &events);
  InsertEvent("msm_bus", "bus_agg_bw", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateMembusTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "memory_bus", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateIdleTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("power", "cpu_idle", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateDiskTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("f2fs", "f2fs_sync_file_enter", &events);
  InsertEvent("f2fs", "f2fs_sync_file_exit", &events);
  InsertEvent("f2fs", "f2fs_write_begin", &events);
  InsertEvent("f2fs", "f2fs_write_end", &events);
  InsertEvent("f2fs", "f2fs_iostat", &events);
  InsertEvent("f2fs", "f2fs_iostat_latency", &events);
  InsertEvent("ext4", "ext4_da_write_begin", &events);
  InsertEvent("ext4", "ext4_da_write_end", &events);
  InsertEvent("ext4", "ext4_sync_file_enter", &events);
  InsertEvent("ext4", "ext4_sync_file_exit", &events);
  InsertEvent("block", "block_bio_queue", &events);
  InsertEvent("block", "block_bio_complete", &events);
  InsertEvent("ufs", "ufshcd_command", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateMmcTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "mmc", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateLoadTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "cpufreq_interactive", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateSyncTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  // linux kernel < 4.9
  AddEventGroup(table, "sync", &events);
  InsertEvent("sync", "sync_pt", &events);
  InsertEvent("sync", "sync_timeline", &events);
  InsertEvent("sync", "sync_wait", &events);
  // linux kernel == 4.9.x
  AddEventGroup(table, "fence", &events);
  InsertEvent("fence", "fence_annotate_wait_on", &events);
  InsertEvent("fence", "fence_destroy", &events);
  InsertEvent("fence", "fence_emit", &events);
  InsertEvent("fence", "fence_enable_signal", &events);
  InsertEvent("fence", "fence_init", &events);
  InsertEvent("fence", "fence_signaled", &events);
  InsertEvent("fence", "fence_wait_end", &events);
  InsertEvent("fence", "fence_wait_start", &events);
  // linux kernel > 4.9
  AddEventGroup(table, "dma_fence", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateWorkqTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "workqueue", &events);
  InsertEvent("workqueue", "workqueue_queue_work", &events);
  InsertEvent("workqueue", "workqueue_execute_start", &events);
  InsertEvent("workqueue", "workqueue_execute_end", &events);
  InsertEvent("workqueue", "workqueue_activate_work", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateMemreclaimTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  InsertEvent("vmscan", "mm_vmscan_direct_reclaim_begin", &events);
  InsertEvent("vmscan", "mm_vmscan_direct_reclaim_end", &events);
  InsertEvent("vmscan", "mm_vmscan_kswapd_wake", &events);
  InsertEvent("vmscan", "mm_vmscan_kswapd_sleep", &events);
  AddEventGroup(table, "lowmemorykiller", &events);
  InsertEvent("lowmemorykiller", "lowmemory_kill", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateRegulatorsTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "regulator", &events);
  InsertEvent("regulator", "regulator_set_voltage_complete", &events);
  InsertEvent("regulator", "regulator_set_voltage", &events);
  InsertEvent("regulator", "regulator_enable_delay", &events);
  InsertEvent("regulator", "regulator_enable_complete", &events);
  InsertEvent("regulator", "regulator_enable", &events);
  InsertEvent("regulator", "regulator_disable_complete", &events);
  InsertEvent("regulator", "regulator_disable", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateBinderDriverTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("binder", "binder_transaction", &events);
  InsertEvent("binder", "binder_transaction_received", &events);
  InsertEvent("binder", "binder_transaction_alloc_buf", &events);
  InsertEvent("binder", "binder_set_priority", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateBinderLockTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("binder", "binder_lock", &events);
  InsertEvent("binder", "binder_locked", &events);
  InsertEvent("binder", "binder_unlock", &events);
  return events;
}

base::FlatSet<GroupAndName> GeneratePagecacheTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "filemap", &events);
  InsertEvent("filemap", "mm_filemap_delete_from_page_cache", &events);
  InsertEvent("filemap", "mm_filemap_add_to_page_cache", &events);
  InsertEvent("filemap", "filemap_set_wb_err", &events);
  InsertEvent("filemap", "file_check_and_advance_wb_err", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateMemoryTracePoints(Tracefs* ftrace) {
  base::FlatSet<GroupAndName> events;
  // Use rss_stat_throttled if supported
  if (ftrace->SupportsRssStatThrottled()) {
    InsertEvent("synthetic", "rss_stat_throttled", &events);
  } else {
    InsertEvent("kmem", "rss_stat", &events);
  }
  InsertEvent("kmem", "ion_heap_grow", &events);
  InsertEvent("kmem", "ion_heap_shrink", &events);
  // ion_stat supersedes ion_heap_grow / shrink for kernel 4.19+
  InsertEvent("ion", "ion_stat", &events);
  InsertEvent("mm_event", "mm_event_record", &events);
  InsertEvent("dmabuf_heap", "dma_heap_stat", &events);
  InsertEvent("gpu_mem", "gpu_mem_total", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateThermalTracePoints() {
  base::FlatSet<GroupAndName> events;
  InsertEvent("thermal", "thermal_temperature", &events);
  InsertEvent("thermal", "cdev_update", &events);
  return events;
}

base::FlatSet<GroupAndName> GenerateCameraTracePoints(
    const ProtoTranslationTable* table) {
  base::FlatSet<GroupAndName> events;
  AddEventGroup(table, "lwis", &events);
  InsertEvent("lwis", "tracing_mark_write", &events);
  return events;
}

std::map<std::string, base::FlatSet<GroupAndName>>
GeneratePredefinedTracePoints(const ProtoTranslationTable* table,
                              Tracefs* ftrace) {
  // Ideally we should keep this code in sync with:
  // platform/frameworks/native/cmds/atrace/atrace.cpp
  // It's not a disaster if they go out of sync, we can always add the ftrace
  // categories manually server side but this is user friendly and reduces the
  // size of the configs.
  std::map<std::string, base::FlatSet<GroupAndName>> tracepoints;

  tracepoints["gfx"] = GenerateGfxTracePoints(table);
  tracepoints["ion"] = GenerateIonTracePoints();
  tracepoints["sched"] = GenerateSchedTracePoints(table);
  tracepoints["irq"] = GenerateIrqTracePoints(table);
  tracepoints["irqoff"] = GenerateIrqOffTracePoints();
  tracepoints["preemptoff"] = GeneratePreemptoffTracePoints();
  tracepoints["i2c"] = GenerateI2cTracePoints(table);
  tracepoints["freq"] = GenerateFreqTracePoints(table);
  tracepoints["membus"] = GenerateMembusTracePoints(table);
  tracepoints["idle"] = GenerateIdleTracePoints();
  tracepoints["disk"] = GenerateDiskTracePoints();
  tracepoints["mmc"] = GenerateMmcTracePoints(table);
  tracepoints["load"] = GenerateLoadTracePoints(table);
  tracepoints["sync"] = GenerateSyncTracePoints(table);
  tracepoints["workq"] = GenerateWorkqTracePoints(table);
  tracepoints["memreclaim"] = GenerateMemreclaimTracePoints(table);
  tracepoints["regulators"] = GenerateRegulatorsTracePoints(table);
  tracepoints["binder_driver"] = GenerateBinderDriverTracePoints();
  tracepoints["binder_lock"] = GenerateBinderLockTracePoints();
  tracepoints["pagecache"] = GeneratePagecacheTracePoints(table);
  tracepoints["memory"] = GenerateMemoryTracePoints(ftrace);
  tracepoints["thermal"] = GenerateThermalTracePoints();
  tracepoints["camera"] = GenerateCameraTracePoints(table);
  return tracepoints;
}
}  // namespace

std::map<std::string, base::FlatSet<GroupAndName>> GetPredefinedTracePoints(
    const ProtoTranslationTable* table,
    Tracefs* tracefs) {
  return GeneratePredefinedTracePoints(table, tracefs);
}

std::map<std::string, base::FlatSet<GroupAndName>>
GetAccessiblePredefinedTracePoints(const ProtoTranslationTable* table,
                                   Tracefs* tracefs) {
  auto tracepoints = GetPredefinedTracePoints(table, tracefs);

  bool generic_enable = tracefs->IsGenericSetEventWritable();

  std::map<std::string, base::FlatSet<GroupAndName>> accessible_tracepoints;
  for (const auto& [category, events] : tracepoints) {
    base::FlatSet<GroupAndName> accessible_events;
    for (const auto& event : events) {
      if (generic_enable
              ? tracefs->IsEventFormatReadable(event.group(), event.name())
              : tracefs->IsEventAccessible(event.group(), event.name())) {
        accessible_events.insert(event);
      }
    }
    if (!accessible_events.empty()) {
      accessible_tracepoints[category] = accessible_events;
    }
  }
  return accessible_tracepoints;
}
}  // namespace perfetto::predefined_tracepoints
