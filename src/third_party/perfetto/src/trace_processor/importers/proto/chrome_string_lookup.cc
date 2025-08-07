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
#include "protos/perfetto/trace/track_event/chrome_process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_thread_descriptor.pbzero.h"
#include "src/trace_processor/storage/trace_storage.h"

using ::perfetto::protos::pbzero::ChromeProcessDescriptor;
using ::perfetto::protos::pbzero::ChromeThreadDescriptor;

namespace perfetto {
namespace trace_processor {

namespace {

struct ProcessName {
  protos::pbzero::ChromeProcessDescriptor::ProcessType type;
  const char* name;
};

constexpr ProcessName kProcessNames[] = {
    {ChromeProcessDescriptor::PROCESS_UNSPECIFIED, nullptr},
    {ChromeProcessDescriptor::PROCESS_BROWSER, "Browser"},
    {ChromeProcessDescriptor::PROCESS_RENDERER, "Renderer"},
    {ChromeProcessDescriptor::PROCESS_UTILITY, "Utility"},
    {ChromeProcessDescriptor::PROCESS_ZYGOTE, "SandboxHelper"},
    {ChromeProcessDescriptor::PROCESS_GPU, "Gpu"},
    {ChromeProcessDescriptor::PROCESS_PPAPI_PLUGIN, "PpapiPlugin"},
    {ChromeProcessDescriptor::PROCESS_PPAPI_BROKER, "PpapiBroker"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_NETWORK,
     "Service: network.mojom.NetworkService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_TRACING,
     "Service: tracing.mojom.TracingService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_STORAGE,
     "Service: storage.mojom.StorageService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_AUDIO,
     "Service: audio.mojom.AudioService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_DATA_DECODER,
     "Service: data_decoder.mojom.DataDecoderService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_UTIL_WIN,
     "Service: chrome.mojom.UtilWin"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_PROXY_RESOLVER,
     "Service: proxy_resolver.mojom.ProxyResolverFactory"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_CDM,
     "Service: media.mojom.CdmServiceBroker"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_MEDIA_FOUNDATION,
     "Service: media.mojom.MediaFoundationServiceBroker"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_VIDEO_CAPTURE,
     "Service: video_capture.mojom.VideoCaptureService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_UNZIPPER,
     "Service: unzip.mojom.Unzipper"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_MIRRORING,
     "Service: mirroring.mojom.MirroringService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_FILEPATCHER,
     "Service: patch.mojom.FilePatcher"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_TTS,
     "Service: chromeos.tts.mojom.TtsService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_PRINTING,
     "Service: printing.mojom.PrintingService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_QUARANTINE,
     "Service: quarantine.mojom.Quarantine"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_CROS_LOCALSEARCH,
     "Service: chromeos.local_search_service.mojom.LocalSearchService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_CROS_ASSISTANT_AUDIO_DECODER,
     "Service: chromeos.assistant.mojom.AssistantAudioDecoderFactory"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_FILEUTIL,
     "Service: chrome.mojom.FileUtilService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_PRINTCOMPOSITOR,
     "Service: printing.mojom.PrintCompositor"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_PAINTPREVIEW,
     "Service: paint_preview.mojom.PaintPreviewCompositorCollection"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_SPEECHRECOGNITION,
     "Service: media.mojom.SpeechRecognitionService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_XRDEVICE,
     "Service: device.mojom.XRDeviceService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_READICON,
     "Service: chrome.mojom.UtilReadIcon"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_LANGUAGEDETECTION,
     "Service: language_detection.mojom.LanguageDetectionService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_SHARING,
     "Service: sharing.mojom.Sharing"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_MEDIAPARSER,
     "Service: chrome.mojom.MediaParserFactory"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_QRCODEGENERATOR,
     "Service: qrcode_generator.mojom.QRCodeService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_PROFILEIMPORT,
     "Service: chrome.mojom.ProfileImport"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_IME,
     "Service: chromeos.ime.mojom.ImeService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_RECORDING,
     "Service: recording.mojom.RecordingService"},
    {ChromeProcessDescriptor::PROCESS_SERVICE_SHAPEDETECTION,
     "Service: shape_detection.mojom.ShapeDetectionService"},
    {ChromeProcessDescriptor::PROCESS_RENDERER_EXTENSION, "Extension Renderer"},
};

struct ThreadName {
  protos::pbzero::ChromeThreadDescriptor::ThreadType type;
  const char* name;
};

constexpr ThreadName kThreadNames[] = {
    {ChromeThreadDescriptor::THREAD_UNSPECIFIED, nullptr},
    {ChromeThreadDescriptor::THREAD_MAIN, "CrProcessMain"},
    {ChromeThreadDescriptor::THREAD_IO, "ChromeIOThread"},
    {ChromeThreadDescriptor::THREAD_NETWORK_SERVICE, "NetworkService"},
    {ChromeThreadDescriptor::THREAD_POOL_BG_WORKER,
     "ThreadPoolBackgroundWorker&"},
    {ChromeThreadDescriptor::THREAD_POOL_FG_WORKER,
     "ThreadPoolForegroundWorker&"},
    {ChromeThreadDescriptor::THREAD_POOL_BG_BLOCKING,
     "ThreadPoolSingleThreadBackgroundBlocking&"},
    {ChromeThreadDescriptor::THREAD_POOL_FG_BLOCKING,
     "ThreadPoolSingleThreadForegroundBlocking&"},
    {ChromeThreadDescriptor::THREAD_POOL_SERVICE, "ThreadPoolService"},
    {ChromeThreadDescriptor::THREAD_COMPOSITOR, "Compositor"},
    {ChromeThreadDescriptor::THREAD_VIZ_COMPOSITOR, "VizCompositorThread"},
    {ChromeThreadDescriptor::THREAD_COMPOSITOR_WORKER, "CompositorTileWorker&"},
    {ChromeThreadDescriptor::THREAD_SERVICE_WORKER, "ServiceWorkerThread&"},
    {ChromeThreadDescriptor::THREAD_MEMORY_INFRA, "MemoryInfra"},
    {ChromeThreadDescriptor::THREAD_SAMPLING_PROFILER, "StackSamplingProfiler"},

    {ChromeThreadDescriptor::THREAD_BROWSER_MAIN, "CrBrowserMain"},
    {ChromeThreadDescriptor::THREAD_RENDERER_MAIN, "CrRendererMain"},
    {ChromeThreadDescriptor::THREAD_CHILD_IO, "Chrome_ChildIOThread"},
    {ChromeThreadDescriptor::THREAD_BROWSER_IO, "Chrome_IOThread"},
    {ChromeThreadDescriptor::THREAD_UTILITY_MAIN, "CrUtilityMain"},
    {ChromeThreadDescriptor::THREAD_GPU_MAIN, "CrGpuMain"},
    {ChromeThreadDescriptor::THREAD_CACHE_BLOCKFILE, "CacheThread_BlockFile"},
    {ChromeThreadDescriptor::ChromeThreadDescriptor::THREAD_MEDIA, "Media"},
    {ChromeThreadDescriptor::THREAD_AUDIO_OUTPUTDEVICE, "AudioOutputDevice"},
    {ChromeThreadDescriptor::THREAD_GPU_MEMORY, "GpuMemoryThread"},
    {ChromeThreadDescriptor::THREAD_GPU_VSYNC, "GpuVSyncThread"},
    {ChromeThreadDescriptor::THREAD_DXA_VIDEODECODER, "DXVAVideoDecoderThread"},
    {ChromeThreadDescriptor::THREAD_BROWSER_WATCHDOG, "BrowserWatchdog"},
    {
        ChromeThreadDescriptor::THREAD_WEBRTC_NETWORK,
        "WebRTC_Network",
    },
    {ChromeThreadDescriptor::THREAD_WINDOW_OWNER, "Window owner thread"},
    {ChromeThreadDescriptor::THREAD_WEBRTC_SIGNALING, "WebRTC_Signaling"},
    {ChromeThreadDescriptor::THREAD_PPAPI_MAIN, "CrPPAPIMain"},
    {ChromeThreadDescriptor::THREAD_GPU_WATCHDOG, "GpuWatchdog"},
    {ChromeThreadDescriptor::THREAD_SWAPPER, "swapper"},
    {ChromeThreadDescriptor::THREAD_GAMEPAD_POLLING, "Gamepad polling thread"},
    {ChromeThreadDescriptor::THREAD_AUDIO_INPUTDEVICE, "AudioInputDevice"},
    {ChromeThreadDescriptor::THREAD_WEBRTC_WORKER, "WebRTC_Worker"},
    {ChromeThreadDescriptor::THREAD_WEBCRYPTO, "WebCrypto"},
    {ChromeThreadDescriptor::THREAD_DATABASE, "Database thread"},
    {ChromeThreadDescriptor::THREAD_PROXYRESOLVER, "Proxy Resolver"},
    {ChromeThreadDescriptor::THREAD_DEVTOOLSADB, "Chrome_DevToolsADBThread"},
    {ChromeThreadDescriptor::THREAD_NETWORKCONFIGWATCHER,
     "NetworkConfigWatcher"},
    {ChromeThreadDescriptor::THREAD_WASAPI_RENDER, "wasapi_render_thread"},
    {ChromeThreadDescriptor::THREAD_LOADER_LOCK_SAMPLER, "LoaderLockSampler"},
    {ChromeThreadDescriptor::THREAD_COMPOSITOR_GPU, "CompositorGpuThread"},
};

}  // namespace

ChromeStringLookup::ChromeStringLookup(TraceStorage* storage) {
  for (uint32_t i = 0; i < base::ArraySize(kProcessNames); i++) {
    chrome_process_name_ids_[kProcessNames[i].type] =
        kProcessNames[i].name ? storage->InternString(kProcessNames[i].name)
                              : kNullStringId;
  }

  for (uint32_t i = 0; i < base::ArraySize(kThreadNames); i++) {
    chrome_thread_name_ids_[kThreadNames[i].type] =
        kThreadNames[i].name ? storage->InternString(kThreadNames[i].name)
                             : kNullStringId;
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
