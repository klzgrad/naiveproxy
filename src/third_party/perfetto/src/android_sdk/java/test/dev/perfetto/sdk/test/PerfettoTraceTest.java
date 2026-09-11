/*
 * Copyright (C) 2024 The Android Open Source Project
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

package dev.perfetto.sdk.test;

import static com.google.common.truth.Truth.assertThat;
import static dev.perfetto.sdk.PerfettoTrace.Category;
import static perfetto.protos.ChromeLatencyInfoOuterClass.ChromeLatencyInfo.LatencyComponentType.COMPONENT_INPUT_EVENT_LATENCY_BEGIN_RWH;
import static perfetto.protos.ChromeLatencyInfoOuterClass.ChromeLatencyInfo.LatencyComponentType.COMPONENT_INPUT_EVENT_LATENCY_SCROLL_UPDATE_ORIGINAL;

import android.os.Process;
import android.util.ArraySet;
import android.util.Log;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import dev.perfetto.sdk.PerfettoNativeMemoryCleaner.AllocationStats;
import dev.perfetto.sdk.PerfettoTrace;
import dev.perfetto.sdk.PerfettoTrack;
import dev.perfetto.sdk.PerfettoTrackEventBuilder;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.Pipe;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import perfetto.protos.ChromeLatencyInfoOuterClass.ChromeLatencyInfo;
import perfetto.protos.ChromeLatencyInfoOuterClass.ChromeLatencyInfo.ComponentInfo;
import perfetto.protos.DataSourceConfigOuterClass.DataSourceConfig;
import perfetto.protos.DebugAnnotationOuterClass.DebugAnnotation;
import perfetto.protos.DebugAnnotationOuterClass.DebugAnnotationName;
import perfetto.protos.InternedDataOuterClass.InternedData;
import perfetto.protos.SourceLocationOuterClass.SourceLocation;
import perfetto.protos.TraceConfigOuterClass.TraceConfig;
import perfetto.protos.TraceConfigOuterClass.TraceConfig.BufferConfig;
import perfetto.protos.TraceConfigOuterClass.TraceConfig.DataSource;
import perfetto.protos.TraceConfigOuterClass.TraceConfig.TriggerConfig;
import perfetto.protos.TraceConfigOuterClass.TraceConfig.TriggerConfig.Trigger;
import perfetto.protos.TraceOuterClass.Trace;
import perfetto.protos.TracePacketOuterClass.TracePacket;
import perfetto.protos.TrackDescriptorOuterClass.TrackDescriptor;
import perfetto.protos.TrackEventConfigOuterClass.TrackEventConfig;
import perfetto.protos.TrackEventOuterClass.EventCategory;
import perfetto.protos.TrackEventOuterClass.EventName;
import perfetto.protos.TrackEventOuterClass.TrackEvent;

/**
 * This class is used to test the native tracing support. Run this test while tracing on the
 * emulator and then run traceview to view the trace.
 */
@RunWith(AndroidJUnit4.class)
public class PerfettoTraceTest {
  private static final String TAG = "PerfettoTraceTest";
  private static final String FOO = "foo";
  private static final String BAR = "bar";
  private static final String TEXT_ABOVE_4K_SIZE = new String(new char[8192]).replace('\0', 'a');

  private static final Category FOO_CATEGORY = new Category(FOO);
  private static final int MESSAGE = 1234567;
  private static final int MESSAGE_COUNT = 3;

  private final Set<String> mCategoryNames = new ArraySet<>();
  private final Set<String> mEventNames = new ArraySet<>();
  private final Set<String> mDebugAnnotationNames = new ArraySet<>();
  private final Set<String> mTrackNames = new ArraySet<>();

  @Before
  public void setUp() {
    PerfettoTrace.registerWithDebugChecks(true);
    // 'var unused' suppress error-prone warning
    var unused = FOO_CATEGORY.register();

    PerfettoTrackEventBuilder.getNativeAllocationStats().reset();

    mCategoryNames.clear();
    mEventNames.clear();
    mDebugAnnotationNames.clear();
    mTrackNames.clear();
  }

  @Test
  public void testFreeNativeMemoryWhenJavaObjectGCed() {
    TraceConfig traceConfig = getTraceConfig(FOO);
    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());
    for (int i = 0; i < 600_000; i++) {
      String eventName = "event_" + i;
      String nativeStringArgKey = "string_key_" + i;
      String nativeStringValue = "string_value_" + i;
      // Create a large amount of 'ArgString' objects in heap to trigger GC, no need to emit them.
      PerfettoTrace.instant(FOO_CATEGORY, eventName).addArg(nativeStringArgKey, nativeStringValue);
    }

    // Manually trigger GC if creating 600_000 objects was not enough.
    for (int i = 0; i < 10; i++) {
      System.runFinalization();
      System.gc();
    }

    // We ignore the trace content.
    byte[] traceBytes = session.close();
    assertThat(traceBytes).isNotEmpty();

    // We test that the GC triggers 'free native memory' function when the corresponding java
    // objects are garbage collected.
    AllocationStats allocationStats = PerfettoTrackEventBuilder.getNativeAllocationStats();
    String argClsName = "dev.perfetto.sdk.PerfettoTrackEventExtra$Arg";
    assertThat(allocationStats.getAllocCountForTarget(argClsName)).isEqualTo(600_000);
    // Assert that the native memory was freed at least once.
    // In practice the counter is usually greater than 300_000 if not manually trigger GC,
    // and 599_995 (600_000 - dev.perfetto.sdk.PerfettoTrackEventBuilder#DEFAULT_EXTRA_CACHE_SIZE)
    // if do manually trigger.
    assertThat(allocationStats.getFreeCountForTarget(argClsName)).isGreaterThan(0);
    String allocDebugStats = allocationStats.reportStats();
    Log.d(TAG, "Memory cleaner allocation stats: " + allocDebugStats);
  }

  @Test
  public void testCategoryWithTags() throws Exception {
    Category category = new Category("MyCategory", List.of("MyTag", "MyOtherTag")).register();
    TraceConfig traceConfig = getTraceConfig(null, List.of("MyTag"));

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());
    PerfettoTrace.instant(category, "event").addArg("arg", 42).emit();

    byte[] traceBytes = session.close();
    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
      }
      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(mDebugAnnotationNames).contains("arg");
    assertThat(mEventNames).contains("event");
    assertThat(mCategoryNames).contains("MyCategory");
  }

  @Test
  public void testDebugAnnotations() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(FOO_CATEGORY, "event")
        .addFlow(2)
        .addFlow(3)
        .addTerminatingFlow(4)
        .addTerminatingFlow(5)
        .addArg("long_val", 10000000000L)
        .addArg("bool_val", true)
        .addArg("double_val", 3.14)
        .addArg("string_val", FOO)
        .emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasDebugAnnotations = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_INSTANT.equals(event.getType())
            && event.getDebugAnnotationsCount() == 4
            && event.getFlowIdsCount() == 2
            && event.getTerminatingFlowIdsCount() == 2) {
          hasDebugAnnotations = true;

          List<DebugAnnotation> annotations = event.getDebugAnnotationsList();

          assertThat(annotations.get(0).getIntValue()).isEqualTo(10000000000L);
          assertThat(annotations.get(1).getBoolValue()).isTrue();
          assertThat(annotations.get(2).getDoubleValue()).isEqualTo(3.14);
          assertThat(annotations.get(3).getStringValue()).isEqualTo(FOO);

          // Flow IDs are transformed by PerfettoTeProcessScopedFlow in
          // include/perfetto/public/track_event.h
          // so we cannot assert for specific values. Instead, we check that
          // there are exactly 2 distinct elements in each list.
          assertThat(new HashSet<>(event.getFlowIdsList())).hasSize(2);
          assertThat(new HashSet<>(event.getTerminatingFlowIdsList())).hasSize(2);
        }
      }

      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasDebugAnnotations).isTrue();
    assertThat(mCategoryNames).contains(FOO);

    assertThat(mDebugAnnotationNames).contains("long_val");
    assertThat(mDebugAnnotationNames).contains("bool_val");
    assertThat(mDebugAnnotationNames).contains("double_val");
    assertThat(mDebugAnnotationNames).contains("string_val");
  }

  @Test
  public void testNamedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.begin(FOO_CATEGORY, "event")
        .usingNamedTrack(123, FOO, PerfettoTrace.getProcessTrackUuid())
        .emit();

    PerfettoTrace.end(FOO_CATEGORY)
        .usingNamedTrack(456, "bar", PerfettoTrace.getThreadTrackUuid(Process.myTid()))
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean hasTrackEvent = false;
    boolean hasTrackUuid = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_SLICE_BEGIN.equals(event.getType()) && event.hasTrackUuid()) {
          hasTrackUuid = true;
        }

        if (TrackEvent.Type.TYPE_SLICE_END.equals(event.getType()) && event.hasTrackUuid()) {
          hasTrackUuid &= true;
        }
      }

      collectInternedData(packet);
      collectTrackNames(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasTrackUuid).isTrue();
    assertThat(mCategoryNames).contains(FOO);
    assertThat(mTrackNames).contains(FOO);
    assertThat(mTrackNames).contains("bar");
  }

  @Test
  public void testNestedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrack parent = PerfettoTrack.process("parent_track");
    PerfettoTrack child = parent.child("child_track");
    PerfettoTrace.instant(FOO_CATEGORY, "event").usingTrack(child).emit();

    Trace trace = Trace.parseFrom(session.close());

    // Index every track descriptor by its uuid and capture the event's track.
    Map<Long, TrackDescriptor> descriptorsByUuid = new HashMap<>();
    long eventTrackUuid = 0;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        descriptorsByUuid.put(td.getUuid(), td);
      }
      if (packet.hasTrackEvent()
          && TrackEvent.Type.TYPE_INSTANT.equals(packet.getTrackEvent().getType())
          && packet.getTrackEvent().hasTrackUuid()) {
        eventTrackUuid = packet.getTrackEvent().getTrackUuid();
      }
    }

    // The event is on the leaf (child) track.
    TrackDescriptor childTd = descriptorsByUuid.get(eventTrackUuid);
    assertThat(childTd).isNotNull();
    assertThat(childTd.getStaticName()).isEqualTo("child_track");

    // The child is nested under the parent track.
    TrackDescriptor parentTd = descriptorsByUuid.get(childTd.getParentUuid());
    assertThat(parentTd).isNotNull();
    assertThat(parentTd.getStaticName()).isEqualTo("parent_track");

    // The parent track is rooted at the process track.
    assertThat(parentTd.getParentUuid()).isEqualTo(PerfettoTrace.getProcessTrackUuid());
  }

  @Test
  public void testGlobalNestedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrack parent = PerfettoTrack.global("global_parent");
    PerfettoTrack child = parent.child("global_child");
    PerfettoTrace.instant(FOO_CATEGORY, "event").usingTrack(child).emit();

    Trace trace = Trace.parseFrom(session.close());

    // Index every track descriptor by its uuid and capture the event's track.
    Map<Long, TrackDescriptor> descriptorsByUuid = new HashMap<>();
    long eventTrackUuid = 0;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        descriptorsByUuid.put(td.getUuid(), td);
      }
      if (packet.hasTrackEvent()
          && TrackEvent.Type.TYPE_INSTANT.equals(packet.getTrackEvent().getType())
          && packet.getTrackEvent().hasTrackUuid()) {
        eventTrackUuid = packet.getTrackEvent().getTrackUuid();
      }
    }

    // The event is on the leaf (child) track.
    TrackDescriptor childTd = descriptorsByUuid.get(eventTrackUuid);
    assertThat(childTd).isNotNull();
    assertThat(childTd.getStaticName()).isEqualTo("global_child");

    // The child is nested under the global parent track.
    TrackDescriptor parentTd = descriptorsByUuid.get(childTd.getParentUuid());
    assertThat(parentTd).isNotNull();
    assertThat(parentTd.getStaticName()).isEqualTo("global_parent");

    // A global root has no process/thread anchor: the outermost named level
    // hangs off uuid 0.
    assertThat(parentTd.getParentUuid()).isEqualTo(0);
  }

  @Test
  public void testSortedNestedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    // "render" orders its children explicitly; each child sets its rank.
    PerfettoTrack render =
        PerfettoTrack.process("render").setChildOrdering(PerfettoTrack.CHILD_ORDERING_EXPLICIT);
    PerfettoTrack gpu = render.child("gpu").setSiblingOrderRank(1);
    PerfettoTrack cpu = render.child("cpu").setSiblingOrderRank(2);
    PerfettoTrace.instant(FOO_CATEGORY, "gpu_work").usingTrack(gpu).emit();
    PerfettoTrace.instant(FOO_CATEGORY, "cpu_work").usingTrack(cpu).emit();

    Trace trace = Trace.parseFrom(session.close());

    Map<String, TrackDescriptor> byName = indexDescriptorsByStaticName(trace);

    // The parent declares explicit child ordering.
    assertThat(byName.get("render").getChildOrdering())
        .isEqualTo(TrackDescriptor.ChildTracksOrdering.EXPLICIT);
    // Each child carries its sibling_order_rank.
    assertThat(byName.get("gpu").getSiblingOrderRank()).isEqualTo(1);
    assertThat(byName.get("cpu").getSiblingOrderRank()).isEqualTo(2);
  }

  @Test
  public void testMergedNestedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrack parent = PerfettoTrack.process("merge_parent");
    // Merged with same-keyed siblings via a string key.
    PerfettoTrack strKeyed =
        parent
            .child("str_keyed")
            .setSiblingMergeBehavior(PerfettoTrack.SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY)
            .setSiblingMergeKey("merge_group_a");
    // Merged with same-keyed siblings via an integer key.
    PerfettoTrack intKeyed =
        parent
            .child("int_keyed")
            .setSiblingMergeBehavior(PerfettoTrack.SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY)
            .setSiblingMergeKey(42);
    // Never merged with any sibling.
    PerfettoTrack lone =
        parent.child("lone").setSiblingMergeBehavior(PerfettoTrack.SIBLING_MERGE_BEHAVIOR_NONE);
    PerfettoTrack keyWithoutBehavior =
        parent.child("key_without_behavior").setSiblingMergeKey("x");
    PerfettoTrace.instant(FOO_CATEGORY, "event_a").usingTrack(strKeyed).emit();
    PerfettoTrace.instant(FOO_CATEGORY, "event_b").usingTrack(intKeyed).emit();
    PerfettoTrace.instant(FOO_CATEGORY, "event_c").usingTrack(lone).emit();
    PerfettoTrace.instant(FOO_CATEGORY, "event_d").usingTrack(keyWithoutBehavior).emit();

    Trace trace = Trace.parseFrom(session.close());

    Map<String, TrackDescriptor> byName = indexDescriptorsByStaticName(trace);

    TrackDescriptor strKeyedTd = byName.get("str_keyed");
    assertThat(strKeyedTd.getSiblingMergeBehavior())
        .isEqualTo(TrackDescriptor.SiblingMergeBehavior.SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY);
    assertThat(strKeyedTd.getSiblingMergeKey()).isEqualTo("merge_group_a");
    assertThat(strKeyedTd.hasSiblingMergeKeyInt()).isFalse();

    TrackDescriptor intKeyedTd = byName.get("int_keyed");
    assertThat(intKeyedTd.getSiblingMergeBehavior())
        .isEqualTo(TrackDescriptor.SiblingMergeBehavior.SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY);
    assertThat(intKeyedTd.getSiblingMergeKeyInt()).isEqualTo(42);
    assertThat(intKeyedTd.hasSiblingMergeKey()).isFalse();

    TrackDescriptor loneTd = byName.get("lone");
    assertThat(loneTd.getSiblingMergeBehavior())
        .isEqualTo(TrackDescriptor.SiblingMergeBehavior.SIBLING_MERGE_BEHAVIOR_NONE);
    // No merge key without BY_SIBLING_MERGE_KEY.
    assertThat(loneTd.hasSiblingMergeKey()).isFalse();
    assertThat(loneTd.hasSiblingMergeKeyInt()).isFalse();

    TrackDescriptor keyWithoutBehaviorTd = byName.get("key_without_behavior");
    assertThat(keyWithoutBehaviorTd.hasSiblingMergeKey()).isFalse();
    assertThat(keyWithoutBehaviorTd.hasSiblingMergeKeyInt()).isFalse();
  }

  /** Indexes every TrackDescriptor in {@code trace} with a static name by that name. */
  private static Map<String, TrackDescriptor> indexDescriptorsByStaticName(Trace trace) {
    Map<String, TrackDescriptor> byName = new HashMap<>();
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        if (!td.getStaticName().isEmpty()) {
          byName.put(td.getStaticName(), td);
        }
      }
    }
    return byName;
  }

  @Test
  public void testCorrelationId() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(FOO_CATEGORY, "int_correlated").setCorrelationId(1234).emit();
    PerfettoTrace.instant(FOO_CATEGORY, "str_correlated").setCorrelationId("req-5678").emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean hasIntCorrelationId = false;
    boolean hasStrCorrelationId = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (!packet.hasTrackEvent()) {
        continue;
      }
      TrackEvent event = packet.getTrackEvent();
      if (event.hasCorrelationId() && event.getCorrelationId() == 1234) {
        hasIntCorrelationId = true;
      }
      if (event.hasCorrelationIdStr() && "req-5678".equals(event.getCorrelationIdStr())) {
        hasStrCorrelationId = true;
      }
    }

    assertThat(hasIntCorrelationId).isTrue();
    assertThat(hasStrCorrelationId).isTrue();
  }

  @Test
  public void testProcessThreadNamedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.begin(FOO_CATEGORY, "event").usingProcessNamedTrack(123, FOO).emit();

    PerfettoTrace.end(FOO_CATEGORY).usingThreadNamedTrack(456, "bar", Process.myTid()).emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean hasTrackEvent = false;
    boolean hasTrackUuid = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_SLICE_BEGIN.equals(event.getType()) && event.hasTrackUuid()) {
          hasTrackUuid = true;
        }

        if (TrackEvent.Type.TYPE_SLICE_END.equals(event.getType()) && event.hasTrackUuid()) {
          hasTrackUuid &= true;
        }
      }

      collectInternedData(packet);
      collectTrackNames(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasTrackUuid).isTrue();
    assertThat(mCategoryNames).contains(FOO);
    assertThat(mTrackNames).contains(FOO);
    assertThat(mTrackNames).contains("bar");
  }

  @Test
  public void testStaticNamedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.begin(FOO_CATEGORY, "event")
        .usingProcessNamedTrack(123, "static_track")
        .emit();

    PerfettoTrace.end(FOO_CATEGORY)
        .usingProcessNamedTrack(123, "static_track")
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean foundStaticName = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        if ("static_track".equals(td.getStaticName())) {
          foundStaticName = true;
        }
      }
    }

    assertThat(foundStaticName).isTrue();
  }

  @Test
  public void testDynamicNamedTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.begin(FOO_CATEGORY, "event")
        .usingProcessNamedTrackWithDynamicName(123, "dynamic_track")
        .emit();

    PerfettoTrace.end(FOO_CATEGORY)
        .usingProcessNamedTrackWithDynamicName(123, "dynamic_track")
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean foundDynamicName = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        if ("dynamic_track".equals(td.getName())) {
          foundDynamicName = true;
        }
      }
    }

    assertThat(foundDynamicName).isTrue();
  }

  @Test
  public void testStaticCounterTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.counter(FOO_CATEGORY, 42)
        .usingProcessCounterTrack("static_counter")
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean foundStaticName = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        if ("static_counter".equals(td.getStaticName())) {
          foundStaticName = true;
        }
      }
    }

    assertThat(foundStaticName).isTrue();
  }

  @Test
  public void testDynamicCounterTrack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.counter(FOO_CATEGORY, 42)
        .usingProcessCounterTrackWithDynamicName("dynamic_counter")
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean foundDynamicName = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackDescriptor()) {
        TrackDescriptor td = packet.getTrackDescriptor();
        if ("dynamic_counter".equals(td.getName())) {
          foundDynamicName = true;
        }
      }
    }

    assertThat(foundDynamicName).isTrue();
  }


  @Test
  public void testCounterSimple() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.counter(FOO_CATEGORY, 16, FOO).emit();

    PerfettoTrace.counter(FOO_CATEGORY, 3.14, "bar").emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean hasTrackEvent = false;
    boolean hasCounterValue = false;
    boolean hasDoubleCounterValue = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_COUNTER.equals(event.getType()) && event.getCounterValue() == 16) {
          hasCounterValue = true;
        }

        if (TrackEvent.Type.TYPE_COUNTER.equals(event.getType())
            && event.getDoubleCounterValue() == 3.14) {
          hasDoubleCounterValue = true;
        }
      }

      collectTrackNames(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasCounterValue).isTrue();
    assertThat(hasDoubleCounterValue).isTrue();
    assertThat(mTrackNames).contains(FOO);
    assertThat(mTrackNames).contains(BAR);
  }

  @Test
  public void testCounter() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.counter(FOO_CATEGORY, 16)
        .usingCounterTrack(PerfettoTrace.getProcessTrackUuid(), FOO)
        .emit();

    PerfettoTrace.counter(FOO_CATEGORY, 3.14)
        .usingCounterTrack(PerfettoTrace.getThreadTrackUuid(Process.myTid()), "bar")
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean hasTrackEvent = false;
    boolean hasCounterValue = false;
    boolean hasDoubleCounterValue = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_COUNTER.equals(event.getType()) && event.getCounterValue() == 16) {
          hasCounterValue = true;
        }

        if (TrackEvent.Type.TYPE_COUNTER.equals(event.getType())
            && event.getDoubleCounterValue() == 3.14) {
          hasDoubleCounterValue = true;
        }
      }

      collectTrackNames(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasCounterValue).isTrue();
    assertThat(hasDoubleCounterValue).isTrue();
    assertThat(mTrackNames).contains(FOO);
    assertThat(mTrackNames).contains("bar");
  }

  @Test
  public void testProcessThreadCounter() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.counter(FOO_CATEGORY, 16).usingProcessCounterTrack(FOO).emit();

    PerfettoTrace.counter(FOO_CATEGORY, 3.14)
        .usingThreadCounterTrack(Process.myTid(), "bar")
        .emit();

    Trace trace = Trace.parseFrom(session.close());

    boolean hasTrackEvent = false;
    boolean hasCounterValue = false;
    boolean hasDoubleCounterValue = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_COUNTER.equals(event.getType()) && event.getCounterValue() == 16) {
          hasCounterValue = true;
        }

        if (TrackEvent.Type.TYPE_COUNTER.equals(event.getType())
            && event.getDoubleCounterValue() == 3.14) {
          hasDoubleCounterValue = true;
        }
      }

      collectTrackNames(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasCounterValue).isTrue();
    assertThat(hasDoubleCounterValue).isTrue();
    assertThat(mTrackNames).contains(FOO);
    assertThat(mTrackNames).contains("bar");
  }

  @Test
  public void testProto() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(FOO_CATEGORY, "event_proto")
        .beginProto()
        .beginNested(33L)
        .addField(4L, 2L)
        .addField(3, "ActivityManagerService.java:11489")
        .endNested()
        .addField(2001, "AIDL::IActivityManager")
        .endProto()
        .emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasSourceLocation = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_INSTANT.equals(event.getType()) && event.hasSourceLocation()) {
          SourceLocation loc = event.getSourceLocation();
          if ("ActivityManagerService.java:11489".equals(loc.getFunctionName())
              && loc.getLineNumber() == 2) {
            hasSourceLocation = true;
          }
        }
      }

      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasSourceLocation).isTrue();
    assertThat(mCategoryNames).contains(FOO);
  }

  @Test
  public void testProtoWithInterning() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    final long fieldId = 1;
    final long internedTypeId = 2; // InternedData.event_names
    final String stringToIntern = "my_interned_string";

    PerfettoTrace.instant(FOO_CATEGORY, "event_with_interning")
        .beginProto()
        .addFieldWithInterning(fieldId, stringToIntern, internedTypeId)
        .endProto()
        .emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasInternedString = false;

    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasInternedData()) {
        InternedData internedData = packet.getInternedData();
        for (int i = 0; i < internedData.getEventNamesCount(); i++) {
          if (internedData.getEventNames(i).getName().equals(stringToIntern)) {
            hasInternedString = true;
            break;
          }
        }
      }

      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
      }
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasInternedString).isTrue();
  }

  @Test
  public void testProtoWithSlowPath() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(FOO_CATEGORY, "event_proto")
        .beginProto()
        .beginNested(33L)
        .addField(4L, 2L)
        .addField(3, TEXT_ABOVE_4K_SIZE)
        .endNested()
        .addField(2001, "AIDL::IActivityManager")
        .endProto()
        .emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasSourceLocation = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_INSTANT.equals(event.getType()) && event.hasSourceLocation()) {
          SourceLocation loc = event.getSourceLocation();
          if (TEXT_ABOVE_4K_SIZE.equals(loc.getFunctionName()) && loc.getLineNumber() == 2) {
            hasSourceLocation = true;
          }
        }
      }

      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasSourceLocation).isTrue();
    assertThat(mCategoryNames).contains(FOO);
  }

  @Test
  public void testProtoNested() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(FOO_CATEGORY, "event_proto_nested")
        .beginProto()
        .beginNested(29L)
        .beginNested(4L)
        .addField(1L, 2)
        .addField(2L, 20000)
        .endNested()
        .beginNested(4L)
        .addField(1L, 1)
        .addField(2L, 40000)
        .endNested()
        .endNested()
        .endProto()
        .emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasChromeLatencyInfo = false;

    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();

        if (TrackEvent.Type.TYPE_INSTANT.equals(event.getType()) && event.hasChromeLatencyInfo()) {
          ChromeLatencyInfo latencyInfo = event.getChromeLatencyInfo();
          if (latencyInfo.getComponentInfoCount() == 2) {
            hasChromeLatencyInfo = true;
            ComponentInfo cmpInfo1 = latencyInfo.getComponentInfo(0);
            assertThat(cmpInfo1.getComponentType())
                .isEqualTo(COMPONENT_INPUT_EVENT_LATENCY_SCROLL_UPDATE_ORIGINAL);
            assertThat(cmpInfo1.getTimeUs()).isEqualTo(20000);

            ComponentInfo cmpInfo2 = latencyInfo.getComponentInfo(1);
            assertThat(cmpInfo2.getComponentType())
                .isEqualTo(COMPONENT_INPUT_EVENT_LATENCY_BEGIN_RWH);
            assertThat(cmpInfo2.getTimeUs()).isEqualTo(40000);
          }
        }
      }

      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasChromeLatencyInfo).isTrue();
    assertThat(mCategoryNames).contains(FOO);
  }

  @Test
  public void testActivateTrigger() throws Exception {
    TraceConfig traceConfig = getTriggerTraceConfig(FOO, FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(FOO_CATEGORY, "event_trigger").emit();

    PerfettoTrace.activateTrigger(FOO, 1000);

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasChromeLatencyInfo = false;

    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
      }

      collectInternedData(packet);
    }

    assertThat(mCategoryNames).contains(FOO);
  }

  @Test
  public void testRegister() throws Exception {
    TraceConfig traceConfig = getTraceConfig(BAR);

    Category barCategory = new Category(BAR);
    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(barCategory, "event").addArg("before", 1).emit();
    // 'var unused' suppress error-prone warning
    var unused = barCategory.register();

    PerfettoTrace.instant(barCategory, "event").addArg("after", 1).emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    for (TracePacket packet : trace.getPacketList()) {
      TrackEvent event;
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        event = packet.getTrackEvent();
      }

      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(mCategoryNames).contains(BAR);

    assertThat(mDebugAnnotationNames).containsExactly("after");
  }

  @Test
  public void testCategoryRegisterAndEnable() {
    Category barCategory = new Category(BAR);
    assertThat(barCategory.getPtr()).isEqualTo(0L);
    assertThat(barCategory.isRegistered()).isFalse();
    assertThat(barCategory.isEnabled()).isFalse();

    barCategory.register();
    assertThat(barCategory.getPtr()).isNotEqualTo(0L);
    assertThat(barCategory.isRegistered()).isTrue();
    assertThat(barCategory.isEnabled()).isFalse();

    TraceConfig traceConfig = getTraceConfig(BAR);
    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());
    assertThat(barCategory.isEnabled()).isTrue();

    session.close();
    assertThat(barCategory.isEnabled()).isFalse();
  }

  @Test
  public void testDisabledCategory() throws Exception {
    class DisabledCategory extends Category {
      public DisabledCategory(String name) {
        super(name);
      }

      @Override
      public boolean isEnabled() {
        return false;
      }
    }

    Category disabledFooCategory = new DisabledCategory("DisabledFoo");

    TraceConfig traceConfig = getTraceConfig(List.of(FOO, "DisabledFoo"));
    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    PerfettoTrace.instant(disabledFooCategory, "disabledEvent").addArg("disabledArg", 1).emit();
    PerfettoTrace.instant(FOO_CATEGORY, "event").addArg("arg", 1).emit();

    byte[] traceBytes = session.close();
    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
      }
      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(mCategoryNames).containsExactly(FOO);
    assertThat(mEventNames).containsExactly("event");
    assertThat(mDebugAnnotationNames).containsExactly("arg");
  }

  private TrackEvent getTrackEvent(Trace trace, int idx) {
    int curIdx = 0;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackEvent()) {
        if (curIdx++ == idx) {
          return packet.getTrackEvent();
        }
      }
    }

    return null;
  }

  private TraceConfig getTraceConfig(List<String> enableCategories, List<String> enableTags) {
    BufferConfig bufferConfig = BufferConfig.newBuilder().setSizeKb(1024).build();
    TrackEventConfig.Builder trackEventConfigBuilder = TrackEventConfig.newBuilder();
    if (enableCategories != null) {
      for (String category : enableCategories) {
        trackEventConfigBuilder.addEnabledCategories(category);
      }
    }
    if (enableTags != null) {
      for (String tag : enableTags) {
        trackEventConfigBuilder.addEnabledTags(tag);
      }
    }
    TrackEventConfig trackEventConfig = trackEventConfigBuilder.build();
    DataSourceConfig dsConfig =
        DataSourceConfig.newBuilder()
            .setName("track_event")
            .setTargetBuffer(0)
            .setTrackEventConfig(trackEventConfig)
            .build();
    DataSource ds = DataSource.newBuilder().setConfig(dsConfig).build();
    TraceConfig traceConfig =
        TraceConfig.newBuilder().addBuffers(bufferConfig).addDataSources(ds).build();
    return traceConfig;
  }

  private TraceConfig getTraceConfig(String enableCategory) {
    return getTraceConfig(List.of(enableCategory));
  }

  private TraceConfig getTraceConfig(List<String> enableCategories) {
    return getTraceConfig(enableCategories, null);
  }

  private TraceConfig getTriggerTraceConfig(String cat, String triggerName) {
    BufferConfig bufferConfig = BufferConfig.newBuilder().setSizeKb(1024).build();
    TrackEventConfig trackEventConfig =
        TrackEventConfig.newBuilder().addEnabledCategories(cat).build();
    DataSourceConfig dsConfig =
        DataSourceConfig.newBuilder()
            .setName("track_event")
            .setTargetBuffer(0)
            .setTrackEventConfig(trackEventConfig)
            .build();
    DataSource ds = DataSource.newBuilder().setConfig(dsConfig).build();
    Trigger trigger = Trigger.newBuilder().setName(triggerName).build();
    TriggerConfig triggerConfig =
        TriggerConfig.newBuilder()
            .setTriggerMode(TriggerConfig.TriggerMode.STOP_TRACING)
            .setTriggerTimeoutMs(1000)
            .addTriggers(trigger)
            .build();
    TraceConfig traceConfig =
        TraceConfig.newBuilder()
            .addBuffers(bufferConfig)
            .addDataSources(ds)
            .setTriggerConfig(triggerConfig)
            .build();
    return traceConfig;
  }

  @Test
  public void testExpensiveDebugCallStack() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    StackTraceElement[] stackTrace =
        new StackTraceElement[] {
          new StackTraceElement("ClassA", "methodA", "FileA.java", 10),
          new StackTraceElement("ClassB", "methodB", "FileB.java", 20),
          new StackTraceElement("ClassC", "methodC", "FileC.java", 30),
          new StackTraceElement("ClassD", "methodD", "FileD.java", 40)
        };

    PerfettoTrace.expensiveDebugCallStack(FOO_CATEGORY, "event_callstack", stackTrace).emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasTrackEvent = false;
    boolean hasCallstack = false;

    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackEvent()) {
        hasTrackEvent = true;
        TrackEvent event = packet.getTrackEvent();
        if (event.hasCallstack()) {
          hasCallstack = true;
          TrackEvent.InlineCallstack callstack = event.getCallstack();
          assertThat(callstack.getFramesCount()).isEqualTo(4);

          TrackEvent.InlineCallstack.Frame frame0 = callstack.getFrames(0);
          assertThat(frame0.getFunctionName()).isEqualTo("ClassD.methodD");
          assertThat(frame0.getSourceFile()).isEqualTo("FileD.java");
          assertThat(frame0.getLineNumber()).isEqualTo(40);

          TrackEvent.InlineCallstack.Frame frame1 = callstack.getFrames(1);
          assertThat(frame1.getFunctionName()).isEqualTo("ClassC.methodC");
          assertThat(frame1.getSourceFile()).isEqualTo("FileC.java");
          assertThat(frame1.getLineNumber()).isEqualTo(30);

          TrackEvent.InlineCallstack.Frame frame2 = callstack.getFrames(2);
          assertThat(frame2.getFunctionName()).isEqualTo("ClassB.methodB");
          assertThat(frame2.getSourceFile()).isEqualTo("FileB.java");
          assertThat(frame2.getLineNumber()).isEqualTo(20);

          TrackEvent.InlineCallstack.Frame frame3 = callstack.getFrames(3);
          assertThat(frame3.getFunctionName()).isEqualTo("ClassA.methodA");
          assertThat(frame3.getSourceFile()).isEqualTo("FileA.java");
          assertThat(frame3.getLineNumber()).isEqualTo(10);
        }
      }
      collectInternedData(packet);
    }

    assertThat(hasTrackEvent).isTrue();
    assertThat(hasCallstack).isTrue();
  }

  @Test
  public void testExpensiveDebugCallStackWithWeight() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);

    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    StackTraceElement[] stackTrace =
        new StackTraceElement[] {
          new StackTraceElement("ClassA", "methodA", "FileA.java", 10),
          new StackTraceElement("ClassB", "methodB", "FileB.java", 20)
        };

    PerfettoTrace.expensiveDebugCallStack(FOO_CATEGORY, "event_callstack", stackTrace, 1234.0)
        .emit();

    byte[] traceBytes = session.close();

    Trace trace = Trace.parseFrom(traceBytes);

    boolean hasWeightedCallstack = false;

    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackEvent()) {
        TrackEvent event = packet.getTrackEvent();
        if (event.hasCallstack()) {
          hasWeightedCallstack = true;
          assertThat(event.getCallstack().getFramesCount()).isEqualTo(2);
          assertThat(event.hasCallstackWeight()).isTrue();
          assertThat(event.getCallstackWeight()).isEqualTo(1234.0);
        }
      }
      collectInternedData(packet);
    }

    assertThat(hasWeightedCallstack).isTrue();
  }

  @Test
  public void testExpensiveDebugCallStackVariousStates() throws Exception {
    TraceConfig traceConfig = getTraceConfig(FOO);
    PerfettoTrace.Session session = new PerfettoTrace.Session(true, traceConfig.toByteArray());

    final Object lock = new Object();
    final Object lockBlock = new Object();
    final Object lockWait = new Object();

    final AtomicBoolean busyThreadStarted = new AtomicBoolean(false);
    final AtomicBoolean blockedThreadStarted = new AtomicBoolean(false);
    final AtomicBoolean waitingThreadStarted = new AtomicBoolean(false);
    final AtomicBoolean nativeBlockedThreadStarted = new AtomicBoolean(false);
    final AtomicBoolean shouldExitWaitingThread = new AtomicBoolean(false);

    // 1. Busy thread (runs for 10 seconds to ensure it is alive during sampling)
    Thread busyThread =
        new Thread(
            () -> {
              synchronized (lock) {
                busyThreadStarted.set(true);
                lock.notifyAll();
              }
              long start = System.currentTimeMillis();
              while (System.currentTimeMillis() - start < 10000) {
                // spin
              }
            },
            "BusyThread");

    // 2. Blocked thread (waiting on lockBlock)
    Thread blockedThread =
        new Thread(
            () -> {
              synchronized (lock) {
                blockedThreadStarted.set(true);
                lock.notifyAll();
              }
              synchronized (lockBlock) {
                // do nothing
              }
            },
            "BlockedThread");

    // 3. Waiting thread (waiting on lockWait)
    Thread waitingThread =
        new Thread(
            () -> {
              synchronized (lock) {
                waitingThreadStarted.set(true);
                lock.notifyAll();
              }
              synchronized (lockWait) {
                while (!shouldExitWaitingThread.get()) {
                  try {
                    lockWait.wait();
                  } catch (InterruptedException e) {
                    // ignore
                  }
                }
              }
            },
            "WaitingThread");

    // 4. Native blocked thread (waiting on pipe read)
    final Pipe pipe = Pipe.open();
    final Pipe.SourceChannel source = pipe.source();
    final Pipe.SinkChannel sink = pipe.sink();

    Thread nativeBlockedThread =
        new Thread(
            () -> {
              synchronized (lock) {
                nativeBlockedThreadStarted.set(true);
                lock.notifyAll();
              }
              try {
                ByteBuffer buf = ByteBuffer.allocate(1);
                source.read(buf); // Blocks here in native read
              } catch (IOException e) {
                // ignore
              }
            },
            "NativeBlockedThread");

    // Start busy thread
    synchronized (lock) {
      busyThread.start();
      while (!busyThreadStarted.get()) {
        lock.wait();
      }
    }

    StackTraceElement[][] traces;

    // Hold lockBlock to keep blockedThread blocked while we sample
    synchronized (lockBlock) {
      // Start blocked thread
      synchronized (lock) {
        blockedThread.start();
        while (!blockedThreadStarted.get()) {
          lock.wait();
        }
      }
      while (blockedThread.getState() != Thread.State.BLOCKED) {
        Thread.sleep(10);
      }

      // Start waiting thread
      synchronized (lock) {
        waitingThread.start();
        while (!waitingThreadStarted.get()) {
          lock.wait();
        }
      }
      while (waitingThread.getState() != Thread.State.WAITING) {
        Thread.sleep(10);
      }

      // Start native blocked thread
      synchronized (lock) {
        nativeBlockedThread.start();
        while (!nativeBlockedThreadStarted.get()) {
          lock.wait();
        }
      }
      // We can't easily poll for native blocked state as it might be RUNNABLE.
      // But starting it and waiting for it to notify us means it has released 'lock'
      // and is proceeding to the read. We sleep a tiny bit to be safe.
      Thread.sleep(100);

      // Sample them
      traces =
          new StackTraceElement[][] {
            busyThread.getStackTrace(),
            blockedThread.getStackTrace(),
            waitingThread.getStackTrace(),
            nativeBlockedThread.getStackTrace()
          };
    }
    // lockBlock released here, blockedThread can exit

    // Clean up pipe to let native blocked thread exit
    sink.write(ByteBuffer.wrap(new byte[] {0}));
    source.close();
    sink.close();

    // Wake up waiting thread so it can exit
    synchronized (lockWait) {
      shouldExitWaitingThread.set(true);
      lockWait.notifyAll();
    }

    for (int i = 0; i < traces.length; i++) {
      Log.i(TAG, "Thread " + i + " stack trace length: " + traces[i].length);
      for (StackTraceElement ste : traces[i]) {
        Log.i(TAG, "  " + ste.toString());
      }
      PerfettoTrace.expensiveDebugCallStack(FOO_CATEGORY, "event_callstack_" + i, traces[i]).emit();
    }

    byte[] traceBytes = session.close();
    Trace trace = Trace.parseFrom(traceBytes);

    int callstackCount = 0;
    for (TracePacket packet : trace.getPacketList()) {
      if (packet.hasTrackEvent()) {
        TrackEvent event = packet.getTrackEvent();
        if (event.hasCallstack()) {
          callstackCount++;
          assertThat(event.getCallstack().getFramesCount()).isGreaterThan(0);
        }
      }
    }

    assertThat(callstackCount).isEqualTo(4);
  }

  private void collectInternedData(TracePacket packet) {
    if (!packet.hasInternedData()) {
      return;
    }

    InternedData data = packet.getInternedData();

    for (EventCategory cat : data.getEventCategoriesList()) {
      mCategoryNames.add(cat.getName());
    }
    for (EventName ev : data.getEventNamesList()) {
      mEventNames.add(ev.getName());
    }
    for (DebugAnnotationName dbg : data.getDebugAnnotationNamesList()) {
      mDebugAnnotationNames.add(dbg.getName());
    }
  }

  private void collectTrackNames(TracePacket packet) {
    if (!packet.hasTrackDescriptor()) {
      return;
    }
    TrackDescriptor desc = packet.getTrackDescriptor();
    if (desc.hasName()) {
      mTrackNames.add(desc.getName());
    }
    if (desc.hasStaticName()) {
      mTrackNames.add(desc.getStaticName());
    }
  }
}
