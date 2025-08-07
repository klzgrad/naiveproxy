// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_BUILTIN_CATEGORIES_H_
#define BASE_TRACE_EVENT_BUILTIN_CATEGORIES_H_

#include "base/base_export.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/tracing_buildflags.h"
#include "build/build_config.h"

PERFETTO_DEFINE_TEST_CATEGORY_PREFIXES("cat",
                                       "foo",
                                       "test",
                                       "kTest",
                                       "noise",
                                       "Testing",
                                       "NotTesting",
                                       TRACE_DISABLED_BY_DEFAULT("test"),
                                       TRACE_DISABLED_BY_DEFAULT("Testing"),
                                       TRACE_DISABLED_BY_DEFAULT("NotTesting"));

// List of builtin category names. If you want to use a new category name in
// your code and you get a static assert, this is the right place to register
// the name.
// See https://perfetto.dev/docs/instrumentation/track-events.
//
// Naming Convention: Follow the `component.category(.sub_category)(.debug)`
// naming convention for new categories.
// Example: `base.scheduling`, `base.scheduling.debug`
//
// Be specific, avoid generic categories. Categories such as `toplevel` become
// junk drawers for many trace events, eventually making them too heavy and
// noisy for specific purposes.
//
// Prefer using ".debug" suffix along with "debug" tag over the legacy
// `DISABLED_BY_DEFAULT()` when creating new debug categories.
// Example: perfetto::Category("cc.debug").SetTags("debug")
// `TRACE_DISABLED_BY_DEFAULT("my_category")` adds
// `disabled-by-default-my_category` prefix and “slow” tag to the category,
// but it doesn’t align with the naming convention, and makes the call sites
// more cluttered compared to the ".debug" suffix.
// Both "slow" and "debug" tags are disabled by default.
//
// Document Categories: Document new categories using `.SetDescription()` and
// optionally identify a suitable owner in comments.
// Use generic tags such as "navigation" to document and group categories.
// Add the "debug" tag for debug categories.
//
// Avoid emitting events to multiple categories (category groups): Category
// groups need to be defined for each combination that’s used in chrome, which
// can lead to combinatorial explosion. They often indicate an issue with how
// existing categories are organized, or are used to group a list of
// categories into another one, such as “devtools.timeline”. Prefer leveraging
// tags to group a set of categories under a common tag instead.
//
// clang-format off
PERFETTO_DEFINE_CATEGORIES_IN_NAMESPACE_WITH_ATTRS(
    base,
    BASE_EXPORT,
    /* The rest of the list is in alphabetical order */
    perfetto::Category("__metadata"),
    perfetto::Category("accessibility"),
    perfetto::Category("AccountFetcherService"),
    perfetto::Category("android.adpf"),
    perfetto::Category("android.ui.jank"),
    perfetto::Category("android_webview"),
    perfetto::Category("android_webview.timeline"),
    perfetto::Category("aogh").SetDescription(
      "Actions on Google Hardware, used in Google-internal code."),
    perfetto::Category("audio").SetTags("audio"),
    perfetto::Category("base").SetTags("toplevel"),
    perfetto::Category("benchmark").SetTags("input"),
    perfetto::Category("blink").SetTags("javascript", "rendering"),
    perfetto::Category("blink.animations"),
    perfetto::Category("blink.bindings"),
    perfetto::Category("blink.console"),
    perfetto::Category("blink.debug.invalidation_tracking")
        .SetDescription(
            "Debugging events for style invalidation, related to "
            "devtools.timeline.InvalidationTracking").SetTags("debug"),
    perfetto::Category("blink.net"),
    perfetto::Category("blink.resource"),
    perfetto::Category("blink.user_timing"),
    perfetto::Category("blink.worker"),
    perfetto::Category("blink_style"),
    perfetto::Category("Blob"),
    perfetto::Category("browser").SetTags("navigation"),
    perfetto::Category("browsing_data"),
    perfetto::Category("CacheStorage"),
    perfetto::Category("Calculators"),
    perfetto::Category("CameraStream"),
    perfetto::Category("camera"),
    perfetto::Category("cast_app"),
    perfetto::Category("cast_perf_test"),
    perfetto::Category("cast.mdns"),
    perfetto::Category("cast.mdns.socket"),
    perfetto::Category("cast.stream"),
    perfetto::Category("cc").SetTags("rendering"),
    perfetto::Category("cc.debug").SetTags("debug"),
    perfetto::Category("cdp.perf"),
    perfetto::Category("chromeos"),
    perfetto::Category("cma"),
    perfetto::Category("compositor"),
    // Config categories do not emit trace events, but are used to configure
    // enabling additional information at runtime, which then is emitted in
    // other trace events.
    perfetto::Category("config.scheduler.record_task_post_time").SetDescription(
      "Controls details emitted by TaskAnnotator::EmitTaskTimingDetails"),
    perfetto::Category("content"),
    perfetto::Category("content_capture"),
    perfetto::Category("cronet"),
    perfetto::Category("interactions"),
    perfetto::Category("delegated_ink_trails"),
    perfetto::Category("device"),
    perfetto::Category("devtools"),
    perfetto::Category("devtools.contrast"),
    perfetto::Category("devtools.timeline"),
    perfetto::Category("disk_cache"),
    perfetto::Category("download"),
    perfetto::Category("download_service"),
    perfetto::Category("drm"),
    perfetto::Category("drmcursor"),
    perfetto::Category("dwrite"),
    perfetto::Category("evdev").SetTags("input"),
    perfetto::Category("event"),
    perfetto::Category("exo"),
    perfetto::Category("extensions"),
    perfetto::Category("explore_sites"),
    perfetto::Category("FileSystem"),
    perfetto::Category("file_system_provider"),
    perfetto::Category("fledge"),
    perfetto::Category("fonts"),
    perfetto::Category("GAMEPAD"),
    perfetto::Category("gpu").SetTags("rendering"),
    perfetto::Category("gpu.angle"),
    perfetto::Category("gpu.angle.texture_metrics"),
    perfetto::Category("gpu.capture").SetTags("video"),
    perfetto::Category("graphics.pipeline"),
    perfetto::Category("headless"),
    perfetto::Category("history").SetDescription(
      "Traces for //components/history."),
    perfetto::Category("hwoverlays"),
    perfetto::Category("identity"),
    perfetto::Category("ime"),
    perfetto::Category("IndexedDB"),
    perfetto::Category("input").SetTags("input"),
    perfetto::Category("input.scrolling").SetTags("input"),
    perfetto::Category("io"),
    perfetto::Category("ipc").SetTags("ipc"),
    perfetto::Category("Java"),
    perfetto::Category("jni"),
    perfetto::Category("jpeg"),
    perfetto::Category("latency"),
    perfetto::Category("latencyInfo"),
    perfetto::Category("leveldb"),
    perfetto::Category("loading").SetTags("navigation"),
    perfetto::Category("log"),
    perfetto::Category("login"),
    perfetto::Category("media").SetTags("video"),
    perfetto::Category("mediastream").SetTags("audio"),
    perfetto::Category("media_router"),
    perfetto::Category("memory"),
    perfetto::Category("midi"),
    perfetto::Category("mojom").SetTags("ipc"),
    perfetto::Category("mojom.flow").SetDescription(
        "Includes flow events related to mojom. Notably, records flows between "
        "senders and receivers.").SetTags("ipc"),
    perfetto::Category("mus"),
    perfetto::Category("native"),
    perfetto::Category("navigation").SetTags("navigation"),
    perfetto::Category("navigation.debug").SetTags("debug"),
    perfetto::Category("net").SetTags("navigation"),
    perfetto::Category("net.stream").SetDescription(
        "Includes events related to creating HTTP streams to serve requests."),
    perfetto::Category("network.scheduler"),
    perfetto::Category("netlog").SetTags("navigation").SetDescription(
      "NetLog events and metadata. Describes the operation of the //net "
      "network stack, e.g. HTTP requests, TLS, DNS, connections, sockets, "
      "etc."),
    perfetto::Category("offline_pages"),
    perfetto::Category("omnibox"),
    perfetto::Category("oobe"),
    perfetto::Category("openscreen"),
    perfetto::Category("ozone"),
    perfetto::Category("partition_alloc"),
    perfetto::Category("passwords"),
    perfetto::Category("p2p").SetTags("audio"),
    perfetto::Category("page-serialization"),
    perfetto::Category("paint_preview"),
    perfetto::Category("pepper"),
    perfetto::Category("performance_scenarios").SetDescription(
        "Includes events when processes enter and leave states defined in "
        "//components/performance_manager/scenario_api/"
        "performance_scenarios.h. For each scenario type, events for "
        "ScenarioScope::kCurrentProcess are emitted to an async track under "
        "each process track, and events for ScenarioScope::kGlobal are emitted "
        "to global async tracks."),
    perfetto::Category("persistent_cache"),
    perfetto::Category("PlatformMalloc"),
    perfetto::Category("power"),
    perfetto::Category("ppapi"),
    perfetto::Category("ppapi_proxy"),
    perfetto::Category("print"),
    perfetto::Category("raf_investigation"),
    perfetto::Category("rail"),
    perfetto::Category("renderer"),
    perfetto::Category("renderer_host"),
    perfetto::Category("renderer.scheduler"),
    perfetto::Category("resources"),
    perfetto::Category("RLZ"),
    perfetto::Category("ServiceWorker"),
    perfetto::Category("SiteEngagement"),
    perfetto::Category("safe_browsing"),
    perfetto::Category("scheduler").SetTags("scheduling"),
    perfetto::Category("scheduler.flow").SetDescription(
        "Includes flow events related to scheduling dependency. Notably, "
        "records flows between tasks running in the thread pool on the same "
        "sequence."),
    perfetto::Category("scheduler.long_tasks"),
    perfetto::Category("screenlock_monitor"),
    perfetto::Category("segmentation_platform"),
    perfetto::Category("sequence_manager").SetTags("scheduling"),
    perfetto::Category("service_manager"),
    perfetto::Category("sharing"),
    perfetto::Category("shell"),
    perfetto::Category("shutdown"),
    perfetto::Category("skia"),
    perfetto::Category("sql"),
    perfetto::Category("stadia_media"),
    perfetto::Category("stadia_rtc"),
    perfetto::Category("startup"),
    perfetto::Category("sync"),
    perfetto::Category("system_apps"),
    perfetto::Category("test_gpu"),
    perfetto::Category("toplevel").SetTags("scheduling", "toplevel"),
    perfetto::Category("toplevel.flow").SetTags("scheduling", "toplevel"),
    perfetto::Category("ui").SetTags("rendering"),
    perfetto::Category("v8"),
    perfetto::Category("v8.execute"),
    perfetto::Category("v8.wasm"),
    perfetto::Category("ValueStoreFrontend::Backend"),
    perfetto::Category("views").SetTags("rendering"),
    perfetto::Category("views.frame"),
    perfetto::Category("viz").SetTags("rendering"),
    perfetto::Category("vk"),
    perfetto::Category("wakeup.flow").SetTags("scheduling"),
    perfetto::Category("wayland"),
    perfetto::Category("webaudio").SetTags("audio"),
    perfetto::Category("webengine.fidl"),
    perfetto::Category("weblayer"),
    perfetto::Category("WebCore"),
    perfetto::Category("webnn"),
    perfetto::Category("webrtc").SetTags("audio", "video"),
    perfetto::Category("webrtc_stats"),
    perfetto::Category("xr"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("android_view_hierarchy")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("animation-worklet")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("audio")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("audio.latency")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("audio-worklet"))
        .SetTags("audio"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("base")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.debug"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.debug.display_lock"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.debug.layout"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.debug.layout.trees"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.feature_usage")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.image_decoding")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("blink.invalidation")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("identifiability")),
    perfetto::Category(
        TRACE_DISABLED_BY_DEFAULT("identifiability.high_entropy_api")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug")).SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.display_items"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.lcd_text"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.picture"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.scheduler"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.scheduler.frames"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cc.debug.scheduler.now"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("content.verbose")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cpu_profiler")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("cpu_profiler.debug"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("crypto.dpapi")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.screenshot")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.timeline")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.timeline.frame")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.timeline.inputs")),
    perfetto::Category(
        TRACE_DISABLED_BY_DEFAULT("devtools.timeline.invalidationTracking")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.timeline.layers")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.timeline.picture")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.timeline.stack")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.target-rundown")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("devtools.v8-source-rundown")),
    perfetto::Category(
        TRACE_DISABLED_BY_DEFAULT("devtools.v8-source-rundown-sources")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("file")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("fonts")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu_cmd_queue")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.dawn")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.debug")).SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.decoder")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.device")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.graphite.dawn")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.service")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("gpu.vulkan.vma")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("histogram_samples")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("java-heap-profiler")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("layer-element")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("layout_shift.debug"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("lifecycles")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("loading")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("mediastream"))
        .SetTags("audio"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("memory-infra")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("memory-infra.v8.code_stats")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("mojom")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("navigation")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("net")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("netlog.sensitive")).SetTags(
      "navigation", "sensitive").SetDescription(
      "NetLog events and metadata, including sensitive information such as "
      "hostnames, URLs, HTTP headers and other identifiable information. "
      "Describes the operation of the //net network stack, e.g. HTTP requests, "
      "TLS, DNS, connections, sockets, etc."),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("network")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("paint-worklet")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("power")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("system_metrics")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("renderer.scheduler.debug"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("sequence_manager")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("sequence_manager.debug"))
        .SetTags("debug"),
    perfetto::Category(
        TRACE_DISABLED_BY_DEFAULT("sequence_manager.verbose_snapshots")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("skia")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("skia.gpu")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("skia.gpu.cache")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("skia.shaders")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("skottie")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("SyncFileSystem")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("system_power")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("thread_pool_diagnostics")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("toplevel.ipc"))
        .SetTags("ipc"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("user_action_samples")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("v8.compile")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("v8.inspector")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("v8.runtime")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("v8.runtime_stats")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("v8.runtime_stats_sampling")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("video_and_image_capture"))
        .SetTags("video"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("display.framedisplayed")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.gpu_composite_time")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.debug.overlay_planes"))
        .SetTags("debug"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.hit_testing_flow")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.overdraw")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.quads")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.surface_id_flow")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.surface_lifetime")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.triangles")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("viz.visual_debugger")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("webaudio.audionode"))
        .SetTags("audio"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("webgpu")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("webnn")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("webrtc"))
        .SetTags("audio", "video"),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("worker.scheduler")),
    perfetto::Category(TRACE_DISABLED_BY_DEFAULT("xr.debug")).SetTags("debug"),
    perfetto::Category::Group("android_webview,toplevel"),
    perfetto::Category::Group("android_webview.timeline,android.ui.jank"),
    perfetto::Category::Group("base,toplevel"),
    perfetto::Category::Group("benchmark,drm"),
    perfetto::Category::Group("benchmark,latencyInfo,rail"),
    perfetto::Category::Group("benchmark,latencyInfo,rail,input.scrolling"),
    perfetto::Category::Group("benchmark,loading"),
    perfetto::Category::Group("benchmark,rail"),
    perfetto::Category::Group("benchmark,uma"),
    perfetto::Category::Group("benchmark,ui"),
    perfetto::Category::Group("benchmark,viz"),
    perfetto::Category::Group(
        "benchmark,viz," TRACE_DISABLED_BY_DEFAULT("display.framedisplayed")),
    perfetto::Category::Group("blink,benchmark"),
    perfetto::Category::Group("blink,benchmark,rail," TRACE_DISABLED_BY_DEFAULT(
        "blink.debug.layout")),
    perfetto::Category::Group("blink,blink.resource"),
    perfetto::Category::Group("blink,blink_style"),
    perfetto::Category::Group("blink,devtools.timeline"),
    perfetto::Category::Group("blink,latency"),
    perfetto::Category::Group("blink,loading"),
    perfetto::Category::Group("blink,rail"),
    perfetto::Category::Group(
        "blink.animations,devtools.timeline,benchmark,rail"),
    perfetto::Category::Group("blink.user_timing,rail"),
    perfetto::Category::Group("browser,content,navigation"),
    perfetto::Category::Group("browser,navigation"),
    perfetto::Category::Group("browser,navigation,benchmark"),
    perfetto::Category::Group("browser,startup"),
    perfetto::Category::Group("category1,category2"),
    perfetto::Category::Group("cc,benchmark"),
    perfetto::Category::Group("cc,benchmark,input,input.scrolling"),
    perfetto::Category::Group("cc,benchmark,latency"),
    perfetto::Category::Group(
        "cc,benchmark," TRACE_DISABLED_BY_DEFAULT("devtools.timeline.frame")),
    perfetto::Category::Group("cc,input"),
    perfetto::Category::Group("cc,raf_investigation"),
    perfetto::Category::Group(
        "cc," TRACE_DISABLED_BY_DEFAULT("devtools.timeline")),
    perfetto::Category::Group("content,navigation"),
    perfetto::Category::Group("devtools.timeline,rail"),
    perfetto::Category::Group("drm,hwoverlays"),
    perfetto::Category::Group("dwrite,fonts"),
    perfetto::Category::Group("fonts,ui"),
    perfetto::Category::Group("gpu,benchmark"),
    perfetto::Category::Group("gpu,benchmark,android_webview"),
    perfetto::Category::Group("gpu,benchmark,webview"),
    perfetto::Category::Group("gpu,login"),
    perfetto::Category::Group("gpu,startup"),
    perfetto::Category::Group("gpu,toplevel.flow"),
    perfetto::Category::Group("gpu.angle,startup"),
    perfetto::Category::Group("input,benchmark"),
    perfetto::Category::Group("input,benchmark,devtools.timeline"),
    perfetto::Category::Group("input,benchmark,devtools.timeline,latencyInfo"),
    perfetto::Category::Group("input,benchmark,latencyInfo"),
    perfetto::Category::Group("input,latency"),
    perfetto::Category::Group("input,rail"),
    perfetto::Category::Group("input,input.scrolling"),
    perfetto::Category::Group("input,views"),
    perfetto::Category::Group("interactions,input.scrolling"),
    perfetto::Category::Group("interactions,startup"),
    perfetto::Category::Group("ipc,security"),
    perfetto::Category::Group("ipc,toplevel"),
    perfetto::Category::Group(
        "Java,devtools," TRACE_DISABLED_BY_DEFAULT("devtools.timeline")),
    perfetto::Category::Group("loading,interactions"),
    perfetto::Category::Group("loading,rail"),
    perfetto::Category::Group("loading,rail,devtools.timeline"),
    perfetto::Category::Group("login,screenlock_monitor"),
    perfetto::Category::Group("media,gpu"),
    perfetto::Category::Group("media,rail"),
    perfetto::Category::Group("navigation,benchmark,rail"),
    perfetto::Category::Group("navigation,rail"),
    perfetto::Category::Group("renderer,benchmark,rail"),
    perfetto::Category::Group("renderer,benchmark,rail,input.scrolling"),
    perfetto::Category::Group("renderer,webkit"),
    perfetto::Category::Group("renderer_host,navigation"),
    perfetto::Category::Group(
        "renderer_host," TRACE_DISABLED_BY_DEFAULT("viz.surface_id_flow")),
    perfetto::Category::Group("scheduler,devtools.timeline,loading"),
    perfetto::Category::Group("shutdown,viz"),
    perfetto::Category::Group("startup,benchmark,rail"),
    perfetto::Category::Group("startup,rail"),
    perfetto::Category::Group("toplevel,graphics.pipeline"),
    perfetto::Category::Group("toplevel,Java"),
    perfetto::Category::Group("toplevel,latency"),
    perfetto::Category::Group("toplevel,mojom"),
    perfetto::Category::Group("toplevel,viz"),
    perfetto::Category::Group("toplevel.flow,mojom.flow"),
    perfetto::Category::Group("ui,input"),
    perfetto::Category::Group("ui,latency"),
    perfetto::Category::Group("ui,toplevel"),
    perfetto::Category::Group("v8," TRACE_DISABLED_BY_DEFAULT("v8.compile")),
    perfetto::Category::Group("v8,devtools.timeline"),
    perfetto::Category::Group(
        "v8,devtools.timeline," TRACE_DISABLED_BY_DEFAULT("v8.compile")),
    perfetto::Category::Group("viz,android.adpf"),
    perfetto::Category::Group("viz,benchmark"),
    perfetto::Category::Group("viz,benchmark,graphics.pipeline"),
    perfetto::Category::Group("viz,benchmark,input.scrolling"),
    perfetto::Category::Group("viz,input.scrolling"),
    perfetto::Category::Group("wakeup.flow,toplevel.flow"),
    perfetto::Category::Group("WebCore,benchmark,rail"),
    perfetto::Category::Group(
        TRACE_DISABLED_BY_DEFAULT("cc.debug") ","
        TRACE_DISABLED_BY_DEFAULT("viz.quads") ","
        TRACE_DISABLED_BY_DEFAULT("devtools.timeline.layers")),
    perfetto::Category::Group(
        TRACE_DISABLED_BY_DEFAULT("cc.debug.display_items") ","
        TRACE_DISABLED_BY_DEFAULT("cc.debug.picture") ","
        TRACE_DISABLED_BY_DEFAULT("devtools.timeline.picture")),
    perfetto::Category::Group(
        TRACE_DISABLED_BY_DEFAULT("v8.inspector") ","
        TRACE_DISABLED_BY_DEFAULT("v8.stack_trace")));
// clang-format on

PERFETTO_USE_CATEGORIES_FROM_NAMESPACE(base);

#endif  // BASE_TRACE_EVENT_BUILTIN_CATEGORIES_H_
