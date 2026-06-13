/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/chrome_string_lookup.h"

#include "perfetto/ext/base/utils.h"
#include "protos/perfetto/trace/track_event/chrome_legacy_ipc.pbzero.h"
#include "protos/third_party/chromium/chrome_enums.pbzero.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace chrome_enums = ::perfetto::protos::chrome_enums::pbzero;

namespace perfetto {
namespace trace_processor {

namespace {

// Add Chrome process and thread names to these two functions to get
// friendly-formatted names in field traces. If an entry is added to
// chrome_enums.proto without adding it here, the enum value name from the proto
// will be used, which is not as easy to understand but at least doesn't lose
// information.
//
// The functions take int params instead of enums to prevent -Wswitch-enum from
// complaining that every enum value isn't handled explicitly.

// Returns a name, which may be null, for `process_type`.
const char* GetProcessNameString(int32_t process_type,
                                 bool ignore_predefined_names_for_testing) {
  PERFETTO_DCHECK(process_type >= chrome_enums::ProcessType_MIN);
  PERFETTO_DCHECK(process_type <= chrome_enums::ProcessType_MAX);
  if (!ignore_predefined_names_for_testing) {
    switch (process_type) {
      case chrome_enums::PROCESS_UNSPECIFIED:
        return nullptr;
      case chrome_enums::PROCESS_BROWSER:
        return "Browser";
      case chrome_enums::PROCESS_RENDERER:
        return "Renderer";
      case chrome_enums::PROCESS_UTILITY:
        return "Utility";
      case chrome_enums::PROCESS_ZYGOTE:
        return "SandboxHelper";
      case chrome_enums::PROCESS_GPU:
        return "Gpu";
      case chrome_enums::PROCESS_PPAPI_PLUGIN:
        return "PpapiPlugin";
      case chrome_enums::PROCESS_PPAPI_BROKER:
        return "PpapiBroker";
      case chrome_enums::PROCESS_SERVICE_NETWORK:
        return "Service: network.mojom.NetworkService";
      case chrome_enums::PROCESS_SERVICE_TRACING:
        return "Service: tracing.mojom.TracingService";
      case chrome_enums::PROCESS_SERVICE_STORAGE:
        return "Service: storage.mojom.StorageService";
      case chrome_enums::PROCESS_SERVICE_AUDIO:
        return "Service: audio.mojom.AudioService";
      case chrome_enums::PROCESS_SERVICE_DATA_DECODER:
        return "Service: data_decoder.mojom.DataDecoderService";
      case chrome_enums::PROCESS_SERVICE_UTIL_WIN:
        return "Service: chrome.mojom.UtilWin";
      case chrome_enums::PROCESS_SERVICE_PROXY_RESOLVER:
        return "Service: proxy_resolver.mojom.ProxyResolverFactory";
      case chrome_enums::PROCESS_SERVICE_CDM:
        return "Service: media.mojom.CdmServiceBroker";
      case chrome_enums::PROCESS_SERVICE_MEDIA_FOUNDATION:
        return "Service: media.mojom.MediaFoundationServiceBroker";
      case chrome_enums::PROCESS_SERVICE_VIDEO_CAPTURE:
        return "Service: video_capture.mojom.VideoCaptureService";
      case chrome_enums::PROCESS_SERVICE_UNZIPPER:
        return "Service: unzip.mojom.Unzipper";
      case chrome_enums::PROCESS_SERVICE_MIRRORING:
        return "Service: mirroring.mojom.MirroringService";
      case chrome_enums::PROCESS_SERVICE_FILEPATCHER:
        return "Service: patch.mojom.FilePatcher";
      case chrome_enums::PROCESS_SERVICE_TTS:
        return "Service: chromeos.tts.mojom.TtsService";
      case chrome_enums::PROCESS_SERVICE_PRINTING:
        return "Service: printing.mojom.PrintingService";
      case chrome_enums::PROCESS_SERVICE_QUARANTINE:
        return "Service: quarantine.mojom.Quarantine";
      case chrome_enums::PROCESS_SERVICE_CROS_LOCALSEARCH:
        return "Service: "
               "chromeos.local_search_service.mojom.LocalSearchService";
      case chrome_enums::PROCESS_SERVICE_CROS_ASSISTANT_AUDIO_DECODER:
        return "Service: chromeos.assistant.mojom.AssistantAudioDecoderFactory";
      case chrome_enums::PROCESS_SERVICE_FILEUTIL:
        return "Service: chrome.mojom.FileUtilService";
      case chrome_enums::PROCESS_SERVICE_PRINTCOMPOSITOR:
        return "Service: printing.mojom.PrintCompositor";
      case chrome_enums::PROCESS_SERVICE_PAINTPREVIEW:
        return "Service: paint_preview.mojom.PaintPreviewCompositorCollection";
      case chrome_enums::PROCESS_SERVICE_SPEECHRECOGNITION:
        return "Service: media.mojom.SpeechRecognitionService";
      case chrome_enums::PROCESS_SERVICE_XRDEVICE:
        return "Service: device.mojom.XRDeviceService";
      case chrome_enums::PROCESS_SERVICE_READICON:
        return "Service: chrome.mojom.UtilReadIcon";
      case chrome_enums::PROCESS_SERVICE_LANGUAGEDETECTION:
        return "Service: language_detection.mojom.LanguageDetectionService";
      case chrome_enums::PROCESS_SERVICE_SHARING:
        return "Service: sharing.mojom.Sharing";
      case chrome_enums::PROCESS_SERVICE_MEDIAPARSER:
        return "Service: chrome.mojom.MediaParserFactory";
      case chrome_enums::PROCESS_SERVICE_QRCODEGENERATOR:
        return "Service: qrcode_generator.mojom.QRCodeService";
      case chrome_enums::PROCESS_SERVICE_PROFILEIMPORT:
        return "Service: chrome.mojom.ProfileImport";
      case chrome_enums::PROCESS_SERVICE_IME:
        return "Service: chromeos.ime.mojom.ImeService";
      case chrome_enums::PROCESS_SERVICE_RECORDING:
        return "Service: recording.mojom.RecordingService";
      case chrome_enums::PROCESS_SERVICE_SHAPEDETECTION:
        return "Service: shape_detection.mojom.ShapeDetectionService";
      case chrome_enums::PROCESS_RENDERER_EXTENSION:
        return "Extension Renderer";
      default:
        // Fall through to the generated name.
        break;
    }
  }
  return chrome_enums::ProcessType_Name(
      static_cast<chrome_enums::ProcessType>(process_type));
}

// Returns a name, which may be null, for `thread_type`.
const char* GetThreadNameString(int32_t thread_type,
                                bool ignore_predefined_names_for_testing) {
  PERFETTO_DCHECK(thread_type >= chrome_enums::ThreadType_MIN);
  PERFETTO_DCHECK(thread_type <= chrome_enums::ThreadType_MAX);
  if (!ignore_predefined_names_for_testing) {
    switch (thread_type) {
      case chrome_enums::THREAD_UNSPECIFIED:
        return nullptr;
      case chrome_enums::THREAD_MAIN:
        return "CrProcessMain";
      case chrome_enums::THREAD_IO:
        return "ChromeIOThread";
      case chrome_enums::THREAD_NETWORK_SERVICE:
        return "NetworkService";
      case chrome_enums::THREAD_POOL_BG_WORKER:
        return "ThreadPoolBackgroundWorker&";
      case chrome_enums::THREAD_POOL_FG_WORKER:
        return "ThreadPoolForegroundWorker&";
      case chrome_enums::THREAD_POOL_BG_BLOCKING:
        return "ThreadPoolSingleThreadBackgroundBlocking&";
      case chrome_enums::THREAD_POOL_FG_BLOCKING:
        return "ThreadPoolSingleThreadForegroundBlocking&";
      case chrome_enums::THREAD_POOL_SERVICE:
        return "ThreadPoolService";
      case chrome_enums::THREAD_COMPOSITOR:
        return "Compositor";
      case chrome_enums::THREAD_VIZ_COMPOSITOR:
        return "VizCompositorThread";
      case chrome_enums::THREAD_COMPOSITOR_WORKER:
        return "CompositorTileWorker&";
      case chrome_enums::THREAD_SERVICE_WORKER:
        return "ServiceWorkerThread&";
      case chrome_enums::THREAD_MEMORY_INFRA:
        return "MemoryInfra";
      case chrome_enums::THREAD_SAMPLING_PROFILER:
        return "StackSamplingProfiler";
      case chrome_enums::THREAD_BROWSER_MAIN:
        return "CrBrowserMain";
      case chrome_enums::THREAD_RENDERER_MAIN:
        return "CrRendererMain";
      case chrome_enums::THREAD_CHILD_IO:
        return "Chrome_ChildIOThread";
      case chrome_enums::THREAD_BROWSER_IO:
        return "Chrome_IOThread";
      case chrome_enums::THREAD_UTILITY_MAIN:
        return "CrUtilityMain";
      case chrome_enums::THREAD_GPU_MAIN:
        return "CrGpuMain";
      case chrome_enums::THREAD_CACHE_BLOCKFILE:
        return "CacheThread_BlockFile";
      case chrome_enums::THREAD_MEDIA:
        return "Media";
      case chrome_enums::THREAD_AUDIO_OUTPUTDEVICE:
        return "AudioOutputDevice";
      case chrome_enums::THREAD_GPU_MEMORY:
        return "GpuMemoryThread";
      case chrome_enums::THREAD_GPU_VSYNC:
        return "GpuVSyncThread";
      case chrome_enums::THREAD_DXA_VIDEODECODER:
        return "DXVAVideoDecoderThread";
      case chrome_enums::THREAD_BROWSER_WATCHDOG:
        return "BrowserWatchdog";
      case chrome_enums::THREAD_WEBRTC_NETWORK:
        return "WebRTC_Network";
      case chrome_enums::THREAD_WINDOW_OWNER:
        return "Window owner thread";
      case chrome_enums::THREAD_WEBRTC_SIGNALING:
        return "WebRTC_Signaling";
      case chrome_enums::THREAD_PPAPI_MAIN:
        return "CrPPAPIMain";
      case chrome_enums::THREAD_GPU_WATCHDOG:
        return "GpuWatchdog";
      case chrome_enums::THREAD_SWAPPER:
        return "swapper";
      case chrome_enums::THREAD_GAMEPAD_POLLING:
        return "Gamepad polling thread";
      case chrome_enums::THREAD_AUDIO_INPUTDEVICE:
        return "AudioInputDevice";
      case chrome_enums::THREAD_WEBRTC_WORKER:
        return "WebRTC_Worker";
      case chrome_enums::THREAD_WEBCRYPTO:
        return "WebCrypto";
      case chrome_enums::THREAD_DATABASE:
        return "Database thread";
      case chrome_enums::THREAD_PROXYRESOLVER:
        return "Proxy Resolver";
      case chrome_enums::THREAD_DEVTOOLSADB:
        return "Chrome_DevToolsADBThread";
      case chrome_enums::THREAD_NETWORKCONFIGWATCHER:
        return "NetworkConfigWatcher";
      case chrome_enums::THREAD_WASAPI_RENDER:
        return "wasapi_render_thread";
      case chrome_enums::THREAD_LOADER_LOCK_SAMPLER:
        return "LoaderLockSampler";
      case chrome_enums::THREAD_COMPOSITOR_GPU:
        return "CompositorGpuThread";
      default:
        // Fall through to the generated name.
        break;
    }
  }
  return chrome_enums::ThreadType_Name(
      static_cast<chrome_enums::ThreadType>(thread_type));
}

}  // namespace

ChromeStringLookup::ChromeStringLookup(
    TraceStorage* storage,
    bool ignore_predefined_names_for_testing) {
  for (int32_t i = chrome_enums::ProcessType_MIN;
       i <= chrome_enums::ProcessType_MAX; ++i) {
    const char* name =
        GetProcessNameString(i, ignore_predefined_names_for_testing);
    chrome_process_name_ids_[i] =
        name ? storage->InternString(name) : kNullStringId;
  }

  for (int32_t i = chrome_enums::ThreadType_MIN;
       i <= chrome_enums::ThreadType_MAX; ++i) {
    const char* name =
        GetThreadNameString(i, ignore_predefined_names_for_testing);
    chrome_thread_name_ids_[i] =
        name ? storage->InternString(name) : kNullStringId;
  }
}

StringId ChromeStringLookup::GetProcessName(int32_t process_type) const {
  auto process_name_it = chrome_process_name_ids_.find(process_type);
  if (process_name_it != chrome_process_name_ids_.end())
    return process_name_it->second;

  PERFETTO_DLOG("GetProcessName error: Unknown Chrome process type %u",
                process_type);
  return kNullStringId;
}

StringId ChromeStringLookup::GetThreadName(int32_t thread_type) const {
  auto thread_name_it = chrome_thread_name_ids_.find(thread_type);
  if (thread_name_it != chrome_thread_name_ids_.end())
    return thread_name_it->second;

  PERFETTO_DLOG("GetThreadName error: Unknown Chrome thread type %u",
                thread_type);
  return kNullStringId;
}

}  // namespace trace_processor
}  // namespace perfetto
