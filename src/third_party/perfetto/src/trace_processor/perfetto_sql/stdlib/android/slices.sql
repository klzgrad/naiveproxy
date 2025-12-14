--
-- Copyright 2023 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

CREATE PERFETTO FUNCTION _remove_lambda_name(
    -- Raw slice name containing at least one "$"
    name STRING
)
-- Removes everything after the first "$"
RETURNS STRING AS
SELECT
  __intrinsic_strip_hex(substr($name, 0, instr($name, "$")), 2) AS end;

CREATE PERFETTO FUNCTION _standardize_wakelock_slice_name(
    -- Raw slice name
    name STRING
)
-- E.g.: Wakelock(epolld280:system_serve
-- To: Wakelock(epolld<...>:system_serve
RETURNS STRING AS
SELECT
  CASE
    WHEN $name GLOB "Wakelock(epolld*" OR $name GLOB "Wakelock(epollm*"
    THEN CASE
      WHEN instr($name, ":") > 0
      THEN substr(
        $name,
        1,
        CASE
          WHEN $name GLOB "Wakelock(epolld*"
          THEN instr($name, "epolld") + length("epolld") - 1
          WHEN $name GLOB "Wakelock(epollm*"
          THEN instr($name, "epollm") + length("epollm") - 1
          ELSE 0
        END
      ) || "<...>" || substr($name, instr($name, ":"), length($name))
      ELSE __intrinsic_strip_hex($name, 2)
    END
    ELSE __intrinsic_strip_hex($name, 2)
  END;

CREATE PERFETTO FUNCTION _standardize_vsync_slice_name(
    -- Raw slice name containing a reference of "*vsync*"
    name STRING
)
-- standardized name of vsync slices
RETURNS STRING AS
SELECT
  CASE
    WHEN $name GLOB "beginFrame*"
    THEN "beginFrame <...>"
    WHEN $name GLOB "frameIsEarly*"
    THEN "frameIsEarly <...>"
    WHEN $name GLOB "*vsync*in*"
    THEN "vsync in <...>"
    WHEN $name GLOB "present for*vsyncIn*"
    THEN "present for vsync in <...>"
    WHEN $name GLOB "wait for earliest present time*"
    THEN "wait for earliest present time <vsync>"
    WHEN $name GLOB "present for Common Panel*"
    THEN "present for Common Panel"
    WHEN $name GLOB "frameTimelineInfo*"
    THEN "frameTimelineInfo <...>"
    WHEN $name GLOB "*setFrameTimelineInfo*"
    THEN "setFrameTimelineInfo <...>"
    WHEN $name GLOB "*AChoreographer_vsyncCallback*"
    THEN "AChoreographer_vsyncCallback <...>"
    WHEN $name GLOB "unsync-vsync-id=* isSfChoreo=false"
    THEN "unsync-vsync-id=<...> isSfChoreo=false"
    WHEN $name GLOB "unsync-vsync-id=* isSfChoreo=true"
    THEN "unsync-vsync-id=<...> isSfChoreo=true"
    WHEN $name GLOB "Discarding old vsync*"
    THEN "Discarding old vsync"
    WHEN $name GLOB "setDisplayModePtr*"
    THEN "setDisplayModePtr"
    WHEN $name GLOB "adjusting vsync by*"
    THEN "adjusting vsync by <...>"
    WHEN $name GLOB "Not-Adjusting vsync by*"
    THEN "Not-Adjusting vsync by <...>"
    WHEN $name GLOB "dropping stale frameNumber*"
    THEN "dropping stale frameNumber <...>"
    WHEN $name GLOB "Sensor event from com.google.sensor.camera_vsync*"
    THEN "Sensor event from com.google.sensor.camera_vsync <...>"
    WHEN $name GLOB "*DisplayInfo*"
    THEN "DisplayInfo <...>"
    WHEN $name GLOB "Choreographer#onVsync*"
    THEN "Choreographer#onVsync<...>"
    WHEN $name GLOB "appSf alarm*;*VSYNC in*"
    THEN "appSf alarm in <...>; vsync in <...>"
    WHEN $name GLOB "sf alarm*;*VSYNC in*"
    THEN "sf alarm in <...>; vsync in <...>"
    WHEN $name GLOB "app alarm*;*VSYNC in*"
    THEN "app alarm in <...>; vsync in <...>"
    WHEN $name GLOB "FT#*"
    THEN "FT#FrameTrackerEvent"
    WHEN $name GLOB "*isVsyncValid*"
    THEN "isVsyncValid <...>"
    WHEN $name GLOB "onHardwareVsyncRequest*"
    THEN "onHardwareVsyncRequest <...>"
    WHEN $name GLOB "onComposerHalVsync*"
    THEN "onComposerHalVsync <...>"
    WHEN $name GLOB "ensureMinFrameDurationIsKept*mNumVsyncsForFrame=*mPastExpectedPresentTimes.size()=*"
    THEN "ensureMinFrameDurationIsKept mNumVsyncsForFrame=<...> mPastExpectedPresentTimes.size()=<...>"
    WHEN $name GLOB "*lastVsyncDelta*"
    THEN "lastVsyncDelta=<...>"
    WHEN $name GLOB "mLastCommittedVsync in*"
    THEN "mLastCommittedVsync in <...>"
    ELSE __intrinsic_strip_hex($name, 2)
  END;

-- Some slice names have params in them. This functions removes them to make it
-- possible to aggregate by name.
-- Some examples are:
--  - Lock/monitor contention slices. The name includes where the lock
--    contention is in the code. That part is removed.
--  - DrawFrames/ooFrame. The name also includes the frame number.
--  - Apk/oat/dex loading: The name of the apk is removed
CREATE PERFETTO FUNCTION android_standardize_slice_name(
    -- The raw slice name.
    name STRING
)
-- Simplified name.
RETURNS STRING AS
SELECT
  CASE
    WHEN $name GLOB "monitor contention with*"
    THEN "monitor contention with <...>"
    WHEN $name GLOB "SuspendThreadByThreadId*"
    THEN "SuspendThreadByThreadId <...>"
    WHEN $name GLOB "LoadApkAssetsFd*"
    THEN "LoadApkAssetsFd <...>"
    WHEN $name GLOB "relayoutWindow*"
    THEN "relayoutWindow <...>"
    WHEN $name GLOB "android.os.Handler: kotlinx.coroutines*"
    THEN "CoroutineContinuation"
    WHEN $name GLOB "Choreographer#doFrame*"
    THEN "Choreographer#doFrame"
    WHEN $name GLOB "DrawFrames*"
    THEN "DrawFrames"
    WHEN $name GLOB "AssetManager::OpenNonAsset*"
    THEN "AssetManager::OpenNonAsset <...>"
    WHEN $name GLOB "AssetManager::OpenXmlAsset*"
    THEN "AssetManager::OpenXmlAsset <...>"
    WHEN $name GLOB "AssetManager::OpenAsset*"
    THEN "AssetManager::OpenAsset <...>"
    WHEN $name GLOB "*AssetInputStream*"
    THEN "AssetInputStream"
    WHEN $name GLOB "openTypedAssetFile*"
    THEN "openTypedAssetFile <...>"
    WHEN $name GLOB "LoadApkAssets*"
    THEN "LoadApkAssets <...>"
    WHEN $name GLOB "*AssetLoader:*"
    THEN "AssetLoader: <...>"
    WHEN $name GLOB "JIT compiling*"
    THEN "JIT compiling"
    WHEN $name GLOB "requested config :*"
    THEN "requested config : <...>"
    WHEN $name GLOB "/data/app*.apk"
    THEN "APK load"
    WHEN $name GLOB "OpenDexFilesFromOat*"
    THEN "OpenDexFilesFromOat"
    WHEN $name GLOB "Open oat file*"
    THEN "Open oat file"
    WHEN $name GLOB "Open dex file*"
    THEN "Open dex file"
    WHEN $name GLOB "VdexFile*"
    THEN "VdexFile"
    WHEN $name GLOB "GC: Wait For*"
    THEN "Garbage Collector"
    WHEN $name GLOB "prepareDispatchCycleLocked*"
    THEN "prepareDispatchCycleLocked <...>"
    WHEN $name GLOB "enqueueDispatchEntryAndStartDispatchCycleLocked*"
    THEN "enqueueDispatchEntryAndStartDispatchCycleLocked <...>"
    WHEN $name GLOB "Layers: *"
    THEN "Layers: <...>"
    WHEN $name GLOB "* SurfaceView*(BLAST)#*"
    THEN substr($name, instr($name, 'SurfaceView'), instr($name, '#') - instr($name, 'SurfaceView'))
    WHEN $name GLOB "uid=*"
    THEN "uid = <...>"
    WHEN $name GLOB "wakeupap=*"
    THEN "wakeupap = <...>"
    WHEN $name GLOB "launchingActivity#*"
    THEN "launchingActivity#<...>"
    WHEN $name GLOB "updateUidStateUL*"
    THEN "updateUidStateUL <...>"
    WHEN $name GLOB "Wakelock(epolld*" OR $name GLOB "Wakelock(epollm*"
    THEN _standardize_wakelock_slice_name($name)
    WHEN $name GLOB "cpuhp*"
    THEN "cpuhp<...>"
    WHEN $name GLOB "atc*"
    THEN "atc<...>"
    WHEN $name GLOB "[0-9]* Alarm:*"
    THEN "Alarm: <...>"
    WHEN $name GLOB "[0-9]* Wifi:*"
    THEN "Wifi: <...>"
    WHEN $name GLOB "[0-9]* Sensor:*"
    THEN "Sensor: <...>"
    WHEN $name GLOB "*Cellular_data*"
    THEN "Cellular_data <...>"
    WHEN $name GLOB "*GET_ACTIVITY_INFO ModemActivityInfo*"
    THEN "<...>GET_ACTIVITY_INFO ModemActivityInfo <...>"
    WHEN $name GLOB "UNSOL_SIGNAL_STRENGTH*"
    THEN "UNSOL_SIGNAL_STRENGTH <...>"
    WHEN $name GLOB "*GET_CELL_INFO_LIST*"
    THEN "<...> GET_CELL_INFO_LIST <...>"
    WHEN $name GLOB "*VOICE_REGISTRATION_STATE*"
    THEN "<...> VOICE_REGISTRATION_STATE <...>"
    WHEN $name GLOB "*DATA_REGISTRATION_STATE*"
    THEN "<...> DATA_REGISTRATION_STATE <...>"
    -- E.g. InputConsumer processing on ClientState{e1d234a mUid=1234 mPid=1234 mSelfReportedDisplayId=0} (0xb000000000000000)
    -- To InputConsumer processing on ClientState{<...>} (<...>)
    WHEN $name GLOB "InputConsumer processing on ClientState*"
    THEN "InputConsumer processing on ClientState<...>"
    -- E.g. Transaction (ptz-fgd-1-LOCAL_MEDIA_REMOVE_DELETED_ITEMS_SYNC, 11910)
    -- To: Transaction (ptz-fgd-1-LOCAL_MEDIA_REMOVE_DELETED_ITEMS_SYNC, <...>)
    WHEN $name GLOB "Transaction (*, *)"
    THEN CASE
      WHEN $name GLOB "Transaction (Thread-*, *)"
      THEN substr($name, 1, instr($name, "(")) || "Thread-<...>," || substr($name, instr($name, ","), length($name))
      ELSE substr($name, 1, instr($name, ",") + 1) || "<...>)"
    END
    -- E.g. Lock contention on thread list lock (owner tid: 1665)
    -- To: Lock contention on thread list lock <...>
    WHEN $name GLOB "Lock contention on* (*"
    THEN substr($name, 0, instr($name, "(")) || "<...>"
    -- Top level handlers slices heuristics:
    -- E.g. android.os.Handler: com.android.systemui.qs.external.TileServiceManager$1
    -- To: Handler: com.android.systemui.qs.external.TileServiceManager
    WHEN $name GLOB "*Handler: *$*"
    THEN _remove_lambda_name(substr($name, instr($name, "Handler:")))
    -- E.g. : android.view.ViewRootImpl$ViewRootHandler: com.android.systemui.someClass$enableMarquee$1
    -- To: Handler: android.view.ViewRootImpl
    WHEN $name GLOB "*.*.*: *$*"
    THEN "Handler: " || _remove_lambda_name(substr($name, ": "))
    -- E.g.: android.os.AsyncTask$InternalHandler: #1
    -- To: Handler: android.os.AsyncTask
    WHEN $name GLOB "*.*$*: #*"
    THEN "Handler: " || _remove_lambda_name($name)
    WHEN $name GLOB "deliverInputEvent*"
    THEN "deliverInputEvent <...>"
    WHEN lower($name) GLOB "*vsync*"
    THEN _standardize_vsync_slice_name($name)
    ELSE __intrinsic_strip_hex($name, 2)
  END;
