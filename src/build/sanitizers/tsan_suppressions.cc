// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the default suppressions for ThreadSanitizer.
// You can also pass additional suppressions via TSAN_OPTIONS:
// TSAN_OPTIONS=suppressions=/path/to/suppressions. Please refer to
// http://dev.chromium.org/developers/testing/threadsanitizer-tsan-v2
// for more info.

#if defined(THREAD_SANITIZER)

// Please make sure the code below declares a single string variable
// kTSanDefaultSuppressions contains TSan suppressions delimited by newlines.
// See http://dev.chromium.org/developers/testing/threadsanitizer-tsan-v2
// for the instructions on writing suppressions.
char kTSanDefaultSuppressions[] =
    // False positives in libflashplayer.so, libgio.so, libglib.so and
    // libgobject.so.
    // Since we don't instrument them, we cannot reason about the
    // synchronization in them.
    "race:libflashplayer.so\n"
    "race:libgio*.so\n"
    "race:libglib*.so\n"
    "race:libgobject*.so\n"

    // Intentional race in ToolsSanityTest.DataRace in base_unittests.
    "race:base/tools_sanity_unittest.cc\n"

    // Data race on WatchdogCounter [test-only].
    "race:base/threading/watchdog_unittest.cc\n"

    // Data race caused by swapping out the network change notifier with a mock
    // [test-only]. http://crbug.com/927330.
    "race:content/browser/net_info_browsertest.cc\n"

    // http://crbug.com/84094.
    "race:sqlite3StatusSet\n"
    "race:pcache1EnforceMaxPage\n"
    "race:pcache1AllocPage\n"

    // http://crbug.com/120808
    "race:base/threading/watchdog.cc\n"

    // http://crbug.com/157586
    "race:third_party/libvpx/source/libvpx/vp8/decoder/threading.c\n"

    // http://crbug.com/158718
    "race:third_party/ffmpeg/libavcodec/pthread.c\n"
    "race:third_party/ffmpeg/libavcodec/pthread_frame.c\n"
    "race:third_party/ffmpeg/libavcodec/vp8.c\n"
    "race:third_party/ffmpeg/libavutil/mem.c\n"
    "race:*HashFrameForTesting\n"
    "race:third_party/ffmpeg/libavcodec/h264pred.c\n"
    "race:media::ReleaseData\n"

    // http://crbug.com/239359
    "race:media::TestInputCallback::OnData\n"

    // http://crbug.com/244385
    "race:unixTempFileDir\n"

    // http://crbug.com/244755
    "race:v8::internal::Zone::NewExpand\n"

    // http://crbug.com/244774
    "race:webrtc::RTPReceiver::ProcessBitrate\n"
    "race:webrtc::RTPSender::ProcessBitrate\n"
    "race:webrtc::VideoCodingModuleImpl::Decode\n"
    "race:webrtc::RTPSender::SendOutgoingData\n"
    "race:webrtc::LibvpxVp8Encoder::GetEncodedPartitions\n"
    "race:webrtc::LibvpxVp8Encoder::Encode\n"
    "race:webrtc::ViEEncoder::DeliverFrame\n"
    "race:webrtc::vcm::VideoReceiver::Decode\n"
    "race:webrtc::VCMReceiver::FrameForDecoding\n"

    // http://crbug.com/244856
    "race:libpulsecommon*.so\n"

    // http://crbug.com/246968
    "race:webrtc::VideoCodingModuleImpl::RegisterPacketRequestCallback\n"

    // http://crbug.com/257396
    "race:base::trace_event::"

    // http://crbug.com/258479
    "race:SamplingStateScope\n"
    "race:g_trace_state\n"

    // http://crbug.com/258499
    "race:third_party/skia/include/core/SkRefCnt.h\n"

    // http://crbug.com/268924
    "race:base::g_power_monitor\n"
    "race:base::PowerMonitor::PowerMonitor\n"
    "race:base::PowerMonitor::AddObserver\n"
    "race:base::PowerMonitor::RemoveObserver\n"
    "race:base::PowerMonitor::IsOnBatteryPower\n"

    // http://crbug.com/272095
    "race:base::g_top_manager\n"

    // http://crbug.com/308590
    "race:CustomThreadWatcher::~CustomThreadWatcher\n"

    // http://crbug.com/476529
    "deadlock:cc::VideoLayerImpl::WillDraw\n"

    // http://crbug.com/328826
    "race:gLCDOrder\n"
    "race:gLCDOrientation\n"

    // http://crbug.com/328868
    "race:PR_Lock\n"

    // http://crbug.com/333244
    "race:content::"
    "VideoCaptureImplTest::MockVideoCaptureImpl::~MockVideoCaptureImpl\n"

    // http://crbug.com/347538
    "race:sctp_timer_start\n"

    // http://crbug.com/348982
    "race:cricket::P2PTransportChannel::OnConnectionDestroyed\n"
    "race:cricket::P2PTransportChannel::AddConnection\n"

    // http://crbug.com/348984
    "race:sctp_express_handle_sack\n"
    "race:system_base_info\n"

    // http://crbug.com/374135
    "race:media::AlsaWrapper::PcmWritei\n"

    // False positive in libc's tzset_internal, http://crbug.com/379738.
    "race:tzset_internal\n"

    // http://crbug.com/380554
    "deadlock:g_type_add_interface_static\n"

    // http:://crbug.com/386385
    "race:content::AppCacheStorageImpl::DatabaseTask::CallRunCompleted\n"

    // http://crbug.com/397022
    "deadlock:"
    "base::trace_event::TraceEventTestFixture_ThreadOnceBlocking_Test::"
    "TestBody\n"

    // http://crbug.com/415472
    "deadlock:base::trace_event::TraceLog::GetCategoryGroupEnabled\n"

    // http://crbug.com/490856
    "deadlock:content::TracingControllerImpl::SetEnabledOnFileThread\n"

    // https://code.google.com/p/skia/issues/detail?id=3294
    "race:SkBaseMutex::acquire\n"

    // Lock inversion in third party code, won't fix.
    // https://crbug.com/455638
    "deadlock:dbus::Bus::ShutdownAndBlock\n"

    // https://crbug.com/459429
    "race:randomnessPid\n"

    // http://crbug.com/582274
    "race:usrsctp_close\n"

    // http://crbug.com/633145
    "race:third_party/libjpeg_turbo/simd/jsimd_x86_64.c\n"

    // http://crbug.com/691029
    "deadlock:libGLX.so*\n"

    // http://crbug.com/695929
    "race:base::i18n::IsRTL\n"
    "race:base::i18n::SetICUDefaultLocale\n"

    // https://crbug.com/794920
    "race:base::debug::SetCrashKeyString\n"
    "race:crash_reporter::internal::CrashKeyStringImpl::Set\n"

    // http://crbug.com/795110
    "race:third_party/fontconfig/*\n"

    // http://crbug.com/927330
    "race:net::(anonymous namespace)::g_network_change_notifier\n"

    // https://crbug.com/965717
    "race:base::internal::ThreadPoolImplTest_"
    "FileDescriptorWatcherNoOpsAfterShutdown_Test::TestBody\n"

    // https://crbug.com/965722
    "race:content::(anonymous namespace)::CorruptDBRequestHandler\n"

    // https://crbug.com/965724
    "race:content::NetworkServiceRestartBrowserTest::MonitorRequest\n"

    // https://crbug.com/965726
    "race:content::RenderFrameHostManagerUnloadBrowserTest::"
    "MonitorResourceRequest\n"

    // End of suppressions.
    ;  // Please keep this semicolon.

#endif  // THREAD_SANITIZER
