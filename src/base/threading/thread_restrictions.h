// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_THREADING_THREAD_RESTRICTIONS_H_
#define BASE_THREADING_THREAD_RESTRICTIONS_H_

#include "base/base_export.h"
#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"

// -----------------------------------------------------------------------------
// Usage documentation
// -----------------------------------------------------------------------------
//
// Overview:
// This file exposes functions to ban and allow certain slow operations
// on a per-thread basis. To annotate *usage* of such slow operations, refer to
// scoped_blocking_call.h instead.
//
// Specific allowances that can be controlled in this file are:
// - Blocking call: Refers to any call that causes the calling thread to wait
//   off-CPU. It includes but is not limited to calls that wait on synchronous
//   file I/O operations: read or write a file from disk, interact with a pipe
//   or a socket, rename or delete a file, enumerate files in a directory, etc.
//   Acquiring a low contention lock is not considered a blocking call.
//
// - Waiting on a //base sync primitive: Refers to calling one of these methods:
//   - base::WaitableEvent::*Wait*
//   - base::ConditionVariable::*Wait*
//   - base::Process::WaitForExit*
//
// - Long CPU work: Refers to any code that takes more than 100 ms to
//   run when there is no CPU contention and no hard page faults and therefore,
//   is not suitable to run on a thread required to keep the browser responsive
//   (where jank could be visible to the user).
//
// The following disallowance functions are offered:
//  - DisallowBlocking(): Disallows blocking calls on the current thread.
//  - DisallowBaseSyncPrimitives(): Disallows waiting on a //base sync primitive
//    on the current thread.
//  - DisallowUnresponsiveTasks() Disallows blocking calls, waiting on a //base
//    sync primitive, and long cpu work on the current thread.
//
// In addition, scoped-allowance mechanisms are offered to make an exception
// within a scope for a behavior that is normally disallowed.
//  - ScopedAllowBlocking(ForTesting): Allows blocking calls.
//  - ScopedAllowBaseSyncPrimitives(ForTesting)(OutsideBlockingScope): Allow
//    waiting on a //base sync primitive. The OutsideBlockingScope suffix allows
//    uses in a scope where blocking is also disallowed.
//
// Avoid using allowances outside of unit tests. In unit tests, use allowances
// with the suffix "ForTesting".
//
// Prefer making blocking calls from tasks posted to base::ThreadPoolInstance
// with base::MayBlock().
//
// Instead of waiting on a WaitableEvent or a ConditionVariable, prefer putting
// the work that should happen after the wait in a continuation callback and
// post it from where the WaitableEvent or ConditionVariable would have been
// signaled. If something needs to be scheduled after many tasks have executed,
// use base::BarrierClosure.
//
// On Windows, join processes asynchronously using base::win::ObjectWatcher.
//
// Where unavoidable, put ScopedAllow* instances in the narrowest scope possible
// in the caller making the blocking call but no further down. For example: if a
// Cleanup() method needs to do a blocking call, document Cleanup() as blocking
// and add a ScopedAllowBlocking instance in callers that can't avoid making
// this call from a context where blocking is banned, as such:
//
//   void Client::MyMethod() {
//     (...)
//     {
//       // Blocking is okay here because XYZ.
//       ScopedAllowBlocking allow_blocking;
//       my_foo_->Cleanup();
//     }
//     (...)
//   }
//
//   // This method can block.
//   void Foo::Cleanup() {
//     // Do NOT add the ScopedAllowBlocking in Cleanup() directly as that hides
//     // its blocking nature from unknowing callers and defeats the purpose of
//     // these checks.
//     FlushStateToDisk();
//   }
//
// Note: In rare situations where the blocking call is an implementation detail
// (i.e. the impl makes a call that invokes AssertBlockingAllowed() but it
// somehow knows that in practice this will not block), it might be okay to hide
// the ScopedAllowBlocking instance in the impl with a comment explaining why
// that's okay.

class BrowserProcessImpl;
class HistogramSynchronizer;
class KeyStorageLinux;
class NativeBackendKWallet;
class NativeDesktopMediaList;
class StartupTimeBomb;

namespace android_webview {
class AwFormDatabaseService;
class CookieManager;
class ScopedAllowInitGLBindings;
}
namespace audio {
class OutputDevice;
}
namespace blink {
class SourceStream;
class VideoFrameResourceProvider;
}
namespace cc {
class CompletionEvent;
class SingleThreadTaskGraphRunner;
}
namespace chromeos {
class BlockingMethodCaller;
namespace system {
class StatisticsProviderImpl;
}
}
namespace chrome_browser_net {
class Predictor;
}
namespace chrome_cleaner {
class SystemReportComponent;
}
namespace content {
class BrowserGpuChannelHostFactory;
class BrowserMainLoop;
class BrowserProcessSubThread;
class BrowserShutdownProfileDumper;
class BrowserTestBase;
class CategorizedWorkerPool;
class DesktopCaptureDevice;
class DWriteFontLookupTableBuilder;
class GpuProcessTransportFactory;
class NestedMessagePumpAndroid;
class RenderWidgetHostViewMac;
class RTCVideoDecoder;
class RTCVideoDecoderAdapter;
class RTCVideoEncoder;
class SandboxHostLinux;
class ScopedAllowWaitForDebugURL;
class ServiceWorkerContextClient;
class SessionStorageDatabase;
class SoftwareOutputDeviceMus;
class SynchronousCompositor;
class SynchronousCompositorHost;
class SynchronousCompositorSyncCallBridge;
class TextInputClientMac;
class WebContentsViewMac;
}  // namespace content
namespace cronet {
class CronetPrefsManager;
class CronetURLRequestContext;
}  // namespace cronet
namespace dbus {
class Bus;
}
namespace disk_cache {
class BackendImpl;
class InFlightIO;
}
namespace functions {
class ExecScriptScopedAllowBaseSyncPrimitives;
}
namespace history_report {
class HistoryReportJniBridge;
}
namespace gpu {
class GpuChannelHost;
}
namespace leveldb {
class LevelDBMojoProxy;
}
namespace leveldb_env {
class DBTracker;
}
namespace media {
class AudioInputDevice;
class AudioOutputDevice;
class BlockingUrlProtocol;
}
namespace memory_instrumentation {
class OSMetrics;
}
namespace midi {
class TaskService;  // https://crbug.com/796830
}
namespace mojo {
class CoreLibraryInitializer;
class SyncCallRestrictions;
namespace core {
class ScopedIPCSupport;
}
}
namespace printing {
class PrinterQuery;
}
namespace rlz_lib {
class FinancialPing;
}
namespace ui {
class CommandBufferClientImpl;
class CommandBufferLocal;
class GpuState;
class MaterialDesignController;
}
namespace net {
class MultiThreadedCertVerifierScopedAllowBaseSyncPrimitives;
class MultiThreadedProxyResolverScopedAllowJoinOnIO;
class NetworkChangeNotifierMac;
class NetworkConfigWatcherMacThread;
class ScopedAllowThreadJoinForProxyResolverV8Tracing;
namespace internal {
class AddressTrackerLinux;
}
}

namespace remoting {
class AutoThread;
namespace protocol {
class ScopedAllowThreadJoinForWebRtcTransport;
}
}

namespace resource_coordinator {
class TabManagerDelegate;
}

namespace service_manager {
class ServiceProcessLauncher;
}

namespace shell_integration_linux {
class LaunchXdgUtilityScopedAllowBaseSyncPrimitives;
}

namespace ui {
class WindowResizeHelperMac;
}

namespace viz {
class HostGpuMemoryBufferManager;
}

namespace vr {
class VrShell;
}

namespace web {
class WebMainLoop;
class WebSubThread;
}

namespace webrtc {
class DesktopConfigurationMonitor;
}

namespace base {

namespace sequence_manager {
namespace internal {
class TaskQueueImpl;
}
}  // namespace sequence_manager

namespace android {
class JavaHandlerThread;
}

namespace internal {
class TaskTracker;
}

class AdjustOOMScoreHelper;
class FileDescriptorWatcher;
class GetAppOutputScopedAllowBaseSyncPrimitives;
class MessageLoopImpl;
class ScopedAllowThreadRecallForStackSamplingProfiler;
class SimpleThread;
class StackSamplingProfiler;
class Thread;

#if DCHECK_IS_ON()
#define INLINE_IF_DCHECK_IS_OFF BASE_EXPORT
#define EMPTY_BODY_IF_DCHECK_IS_OFF
#else
#define INLINE_IF_DCHECK_IS_OFF inline

// The static_assert() eats follow-on semicolons. `= default` would work
// too, but it makes clang realize that all the Scoped classes are no-ops in
// non-dcheck builds and it starts emitting many -Wunused-variable warnings.
#define EMPTY_BODY_IF_DCHECK_IS_OFF \
  {}                                \
  static_assert(true, "")
#endif

namespace internal {

// Asserts that blocking calls are allowed in the current scope. This is an
// internal call, external code should use ScopedBlockingCall instead, which
// serves as a precise annotation of the scope that may/will block.
INLINE_IF_DCHECK_IS_OFF void AssertBlockingAllowed()
    EMPTY_BODY_IF_DCHECK_IS_OFF;

}  // namespace internal

// Disallows blocking on the current thread.
INLINE_IF_DCHECK_IS_OFF void DisallowBlocking() EMPTY_BODY_IF_DCHECK_IS_OFF;

// Disallows blocking calls within its scope.
class BASE_EXPORT ScopedDisallowBlocking {
 public:
  ScopedDisallowBlocking() EMPTY_BODY_IF_DCHECK_IS_OFF;
  ~ScopedDisallowBlocking() EMPTY_BODY_IF_DCHECK_IS_OFF;

 private:
#if DCHECK_IS_ON()
  const bool was_disallowed_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ScopedDisallowBlocking);
};

class BASE_EXPORT ScopedAllowBlocking {
 private:
  FRIEND_TEST_ALL_PREFIXES(ThreadRestrictionsTest, ScopedAllowBlocking);
  friend class ScopedAllowBlockingForTesting;

  // This can only be instantiated by friends. Use ScopedAllowBlockingForTesting
  // in unit tests to avoid the friend requirement.
  friend class AdjustOOMScoreHelper;
  friend class android_webview::ScopedAllowInitGLBindings;
  friend class content::BrowserProcessSubThread;
  friend class content::GpuProcessTransportFactory;
  friend class content::RenderWidgetHostViewMac;  // http://crbug.com/121917
  friend class content::WebContentsViewMac;
  friend class cronet::CronetPrefsManager;
  friend class cronet::CronetURLRequestContext;
  friend class memory_instrumentation::OSMetrics;
  friend class mojo::CoreLibraryInitializer;
  friend class resource_coordinator::TabManagerDelegate;  // crbug.com/778703
  friend class ui::MaterialDesignController;
  friend class web::WebSubThread;
  friend class StackSamplingProfiler;

  ScopedAllowBlocking() EMPTY_BODY_IF_DCHECK_IS_OFF;
  ~ScopedAllowBlocking() EMPTY_BODY_IF_DCHECK_IS_OFF;

#if DCHECK_IS_ON()
  const bool was_disallowed_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ScopedAllowBlocking);
};

class ScopedAllowBlockingForTesting {
 public:
  ScopedAllowBlockingForTesting() {}
  ~ScopedAllowBlockingForTesting() {}

 private:
#if DCHECK_IS_ON()
  ScopedAllowBlocking scoped_allow_blocking_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ScopedAllowBlockingForTesting);
};

INLINE_IF_DCHECK_IS_OFF void DisallowBaseSyncPrimitives()
    EMPTY_BODY_IF_DCHECK_IS_OFF;

class BASE_EXPORT ScopedAllowBaseSyncPrimitives {
 private:
  // This can only be instantiated by friends. Use
  // ScopedAllowBaseSyncPrimitivesForTesting in unit tests to avoid the friend
  // requirement.
  FRIEND_TEST_ALL_PREFIXES(ThreadRestrictionsTest,
                           ScopedAllowBaseSyncPrimitives);
  FRIEND_TEST_ALL_PREFIXES(ThreadRestrictionsTest,
                           ScopedAllowBaseSyncPrimitivesResetsState);
  FRIEND_TEST_ALL_PREFIXES(ThreadRestrictionsTest,
                           ScopedAllowBaseSyncPrimitivesWithBlockingDisallowed);

  // Allowed usage:
  friend class SimpleThread;
  friend class base::GetAppOutputScopedAllowBaseSyncPrimitives;
  friend class blink::SourceStream;
  friend class chrome_cleaner::SystemReportComponent;
  friend class content::BrowserMainLoop;
  friend class content::BrowserProcessSubThread;
  friend class content::DWriteFontLookupTableBuilder;
  friend class content::ServiceWorkerContextClient;
  friend class content::SessionStorageDatabase;
  friend class functions::ExecScriptScopedAllowBaseSyncPrimitives;
  friend class history_report::HistoryReportJniBridge;
  friend class internal::TaskTracker;
  friend class leveldb::LevelDBMojoProxy;
  friend class leveldb_env::DBTracker;
  friend class media::BlockingUrlProtocol;
  friend class mojo::core::ScopedIPCSupport;
  friend class net::MultiThreadedCertVerifierScopedAllowBaseSyncPrimitives;
  friend class rlz_lib::FinancialPing;
  friend class shell_integration_linux::
      LaunchXdgUtilityScopedAllowBaseSyncPrimitives;
  friend class webrtc::DesktopConfigurationMonitor;

  // Usage that should be fixed:
  friend class ::NativeBackendKWallet;            // http://crbug.com/125331
  friend class ::chromeos::system::
      StatisticsProviderImpl;                      // http://crbug.com/125385
  friend class content::TextInputClientMac;        // http://crbug.com/121917
  friend class blink::VideoFrameResourceProvider;  // http://crbug.com/878070

  ScopedAllowBaseSyncPrimitives() EMPTY_BODY_IF_DCHECK_IS_OFF;
  ~ScopedAllowBaseSyncPrimitives() EMPTY_BODY_IF_DCHECK_IS_OFF;

#if DCHECK_IS_ON()
  const bool was_disallowed_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ScopedAllowBaseSyncPrimitives);
};

class BASE_EXPORT ScopedAllowBaseSyncPrimitivesOutsideBlockingScope {
 private:
  // This can only be instantiated by friends. Use
  // ScopedAllowBaseSyncPrimitivesForTesting in unit tests to avoid the friend
  // requirement.
  FRIEND_TEST_ALL_PREFIXES(ThreadRestrictionsTest,
                           ScopedAllowBaseSyncPrimitivesOutsideBlockingScope);
  FRIEND_TEST_ALL_PREFIXES(
      ThreadRestrictionsTest,
      ScopedAllowBaseSyncPrimitivesOutsideBlockingScopeResetsState);

  // Allowed usage:
  friend class ::BrowserProcessImpl;  // http://crbug.com/125207
  friend class ::KeyStorageLinux;
  friend class ::NativeDesktopMediaList;
  friend class ::StartupTimeBomb;
  friend class android::JavaHandlerThread;
  friend class android_webview::
      AwFormDatabaseService;  // http://crbug.com/904431
  friend class android_webview::CookieManager;
  friend class audio::OutputDevice;
  friend class base::sequence_manager::internal::TaskQueueImpl;
  friend class base::FileDescriptorWatcher;
  friend class base::MessageLoopImpl;
  friend class base::ScopedAllowThreadRecallForStackSamplingProfiler;
  friend class base::StackSamplingProfiler;
  friend class content::DesktopCaptureDevice;
  friend class content::RTCVideoDecoder;
  friend class content::RTCVideoDecoderAdapter;
  friend class content::RTCVideoEncoder;
  friend class content::SandboxHostLinux;
  friend class content::ScopedAllowWaitForDebugURL;
  friend class content::SynchronousCompositor;
  friend class content::SynchronousCompositorHost;
  friend class content::SynchronousCompositorSyncCallBridge;
  friend class media::AudioInputDevice;
  friend class media::AudioOutputDevice;
  friend class mojo::SyncCallRestrictions;
  friend class net::NetworkConfigWatcherMacThread;
  friend class viz::HostGpuMemoryBufferManager;
  friend class vr::VrShell;

  // Usage that should be fixed:
  friend class ::chromeos::BlockingMethodCaller;  // http://crbug.com/125360
  friend class base::Thread;                      // http://crbug.com/918039
  friend class cc::CompletionEvent;               // http://crbug.com/902653
  friend class cc::SingleThreadTaskGraphRunner;   // http://crbug.com/902823
  friend class content::
      BrowserGpuChannelHostFactory;                 // http://crbug.com/125248
  friend class content::CategorizedWorkerPool;      // http://crbug.com/902823
  friend class dbus::Bus;                           // http://crbug.com/125222
  friend class disk_cache::BackendImpl;             // http://crbug.com/74623
  friend class disk_cache::InFlightIO;              // http://crbug.com/74623
  friend class gpu::GpuChannelHost;                 // http://crbug.com/125264
  friend class remoting::protocol::
      ScopedAllowThreadJoinForWebRtcTransport;      // http://crbug.com/660081
  friend class midi::TaskService;                   // https://crbug.com/796830
  friend class net::internal::AddressTrackerLinux;  // http://crbug.com/125097
  friend class net::
      MultiThreadedProxyResolverScopedAllowJoinOnIO;  // http://crbug.com/69710
  friend class net::NetworkChangeNotifierMac;         // http://crbug.com/125097
  friend class net::
      ScopedAllowThreadJoinForProxyResolverV8Tracing;  // http://crbug.com/69710
  friend class printing::PrinterQuery;                 // http://crbug.com/66082
  friend class remoting::AutoThread;  // https://crbug.com/944316
  // Not used in production yet, https://crbug.com/844078.
  friend class service_manager::ServiceProcessLauncher;
  friend class ui::WindowResizeHelperMac;  // http://crbug.com/902829

  ScopedAllowBaseSyncPrimitivesOutsideBlockingScope()
      EMPTY_BODY_IF_DCHECK_IS_OFF;
  ~ScopedAllowBaseSyncPrimitivesOutsideBlockingScope()
      EMPTY_BODY_IF_DCHECK_IS_OFF;

#if DCHECK_IS_ON()
  const bool was_disallowed_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ScopedAllowBaseSyncPrimitivesOutsideBlockingScope);
};

class BASE_EXPORT ScopedAllowBaseSyncPrimitivesForTesting {
 public:
  ScopedAllowBaseSyncPrimitivesForTesting() EMPTY_BODY_IF_DCHECK_IS_OFF;
  ~ScopedAllowBaseSyncPrimitivesForTesting() EMPTY_BODY_IF_DCHECK_IS_OFF;

 private:
#if DCHECK_IS_ON()
  const bool was_disallowed_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ScopedAllowBaseSyncPrimitivesForTesting);
};

namespace internal {

// Asserts that waiting on a //base sync primitive is allowed in the current
// scope.
INLINE_IF_DCHECK_IS_OFF void AssertBaseSyncPrimitivesAllowed()
    EMPTY_BODY_IF_DCHECK_IS_OFF;

// Resets all thread restrictions on the current thread.
INLINE_IF_DCHECK_IS_OFF void ResetThreadRestrictionsForTesting()
    EMPTY_BODY_IF_DCHECK_IS_OFF;

}  // namespace internal

// Asserts that running long CPU work is allowed in the current scope.
INLINE_IF_DCHECK_IS_OFF void AssertLongCPUWorkAllowed()
    EMPTY_BODY_IF_DCHECK_IS_OFF;

INLINE_IF_DCHECK_IS_OFF void DisallowUnresponsiveTasks()
    EMPTY_BODY_IF_DCHECK_IS_OFF;

class BASE_EXPORT ThreadRestrictions {
 public:
  // Constructing a ScopedAllowIO temporarily allows IO for the current
  // thread.  Doing this is almost certainly always incorrect.
  //
  // DEPRECATED. Use ScopedAllowBlocking(ForTesting).
  class BASE_EXPORT ScopedAllowIO {
   public:
    ScopedAllowIO() EMPTY_BODY_IF_DCHECK_IS_OFF;
    ~ScopedAllowIO() EMPTY_BODY_IF_DCHECK_IS_OFF;

   private:
#if DCHECK_IS_ON()
    const bool was_allowed_;
#endif

    DISALLOW_COPY_AND_ASSIGN(ScopedAllowIO);
  };

#if DCHECK_IS_ON()
  // Set whether the current thread to make IO calls.
  // Threads start out in the *allowed* state.
  // Returns the previous value.
  //
  // DEPRECATED. Use ScopedAllowBlocking(ForTesting) or ScopedDisallowBlocking.
  static bool SetIOAllowed(bool allowed);

  // Set whether the current thread can use singletons.  Returns the previous
  // value.
  static bool SetSingletonAllowed(bool allowed);

  // Check whether the current thread is allowed to use singletons (Singleton /
  // LazyInstance).  DCHECKs if not.
  static void AssertSingletonAllowed();

  // Disable waiting on the current thread. Threads start out in the *allowed*
  // state. Returns the previous value.
  //
  // DEPRECATED. Use DisallowBaseSyncPrimitives.
  static void DisallowWaiting();
#else
  // Inline the empty definitions of these functions so that they can be
  // compiled out.
  static bool SetIOAllowed(bool allowed) { return true; }
  static bool SetSingletonAllowed(bool allowed) { return true; }
  static void AssertSingletonAllowed() {}
  static void DisallowWaiting() {}
#endif

 private:
  // DO NOT ADD ANY OTHER FRIEND STATEMENTS.
  // BEGIN ALLOWED USAGE.
  friend class content::BrowserMainLoop;
  friend class content::BrowserShutdownProfileDumper;
  friend class content::BrowserTestBase;
  friend class content::ScopedAllowWaitForDebugURL;
  friend class ::HistogramSynchronizer;
  friend class internal::TaskTracker;
  friend class web::WebMainLoop;
  friend class MessagePumpDefault;
  friend class PlatformThread;
  friend class ui::CommandBufferClientImpl;
  friend class ui::CommandBufferLocal;
  friend class ui::GpuState;

  // END ALLOWED USAGE.
  // BEGIN USAGE THAT NEEDS TO BE FIXED.
  friend class chrome_browser_net::Predictor;     // http://crbug.com/78451
#if !defined(OFFICIAL_BUILD)
  friend class content::SoftwareOutputDeviceMus;  // Interim non-production code
#endif
// END USAGE THAT NEEDS TO BE FIXED.

#if DCHECK_IS_ON()
  // DEPRECATED. Use ScopedAllowBaseSyncPrimitives.
  static bool SetWaitAllowed(bool allowed);
#else
  static bool SetWaitAllowed(bool allowed) { return true; }
#endif

  DISALLOW_IMPLICIT_CONSTRUCTORS(ThreadRestrictions);
};

#undef INLINE_IF_DCHECK_IS_OFF
#undef EMPTY_BODY_IF_DCHECK_IS_OFF

}  // namespace base

#endif  // BASE_THREADING_THREAD_RESTRICTIONS_H_
