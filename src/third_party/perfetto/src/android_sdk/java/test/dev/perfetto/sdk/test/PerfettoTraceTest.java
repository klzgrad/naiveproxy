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
import dev.perfetto.sdk.PerfettoTrackEventBuilder;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
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
    System.loadLibrary("perfetto_jni");
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
    final long internedTypeId = 44; // InternedData.android_job_name
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
        if (internedData.getAndroidJobNameCount() > 0) {
          if (internedData.getAndroidJobName(0).getName().equals(stringToIntern)) {
            hasInternedString = true;
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
    mTrackNames.add(desc.getName());
  }
}
