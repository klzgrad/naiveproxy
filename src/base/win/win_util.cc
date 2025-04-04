// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "base/win/win_util.h"

#include <objbase.h>

#include <initguid.h>
#include <shobjidl.h>
#include <tchar.h>
#include <winternl.h>

#include <aclapi.h>
#include <cfgmgr32.h>
#include <inspectable.h>
#include <lm.h>
#include <mdmregistration.h>
#include <ntstatus.h>
#include <powrprof.h>
#include <propkey.h>
#include <psapi.h>
#include <roapi.h>
#include <sddl.h>
#include <setupapi.h>
#include <shellscalingapi.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <strsafe.h>
#include <tpcshrd.h>
#include <uiviewsettingsinterop.h>
#include <wbemidl.h>
#include <windows.ui.viewmanagement.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/base_switches.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/containers/heap_array.h"
#include "base/debug/alias.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/scoped_native_library.h"
#include "base/strings/string_util.h"
#include "base/strings/string_util_win.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/scoped_thread_priority.h"
#include "base/threading/thread_restrictions.h"
#include "base/timer/elapsed_timer.h"
#include "base/win/access_token.h"
#include "base/win/com_init_util.h"
#include "base/win/core_winrt_util.h"
#include "base/win/propvarutil.h"
#include "base/win/registry.h"
#include "base/win/scoped_bstr.h"
#include "base/win/scoped_co_mem.h"
#include "base/win/scoped_handle.h"
#include "base/win/scoped_hstring.h"
#include "base/win/scoped_propvariant.h"
#include "base/win/scoped_safearray.h"
#include "base/win/scoped_variant.h"
#include "base/win/shlwapi.h"
#include "base/win/static_constants.h"
#include "base/win/windows_version.h"
#include "base/win/wmi.h"

namespace base {
namespace win {

namespace {

using QueryKeyFunction =
    ScopedDeviceConvertibilityStateForTesting::QueryFunction;

// Sets the value of |property_key| to |property_value| in |property_store|.
bool SetPropVariantValueForPropertyStore(
    IPropertyStore* property_store,
    const PROPERTYKEY& property_key,
    const ScopedPropVariant& property_value) {
  DCHECK(property_store);

  HRESULT result = property_store->SetValue(property_key, property_value.get());
  if (result == S_OK) {
    result = property_store->Commit();
  }
  if (SUCCEEDED(result)) {
    return true;
  }
#if DCHECK_IS_ON()
  if (HRESULT_FACILITY(result) == FACILITY_WIN32) {
    ::SetLastError(HRESULT_CODE(result));
  }
  // See third_party/perl/c/i686-w64-mingw32/include/propkey.h for GUID and
  // PID definitions.
  DPLOG(ERROR) << "Failed to set property with GUID "
               << WStringFromGUID(property_key.fmtid) << " PID "
               << property_key.pid;
#endif
  return false;
}

void __cdecl ForceCrashOnSigAbort(int) {
  *((volatile int*)nullptr) = 0x1337;
}

// Returns the current platform role. We use the PowerDeterminePlatformRoleEx
// API for that.
POWER_PLATFORM_ROLE GetPlatformRole() {
  return PowerDeterminePlatformRoleEx(POWER_PLATFORM_ROLE_V2);
}

// Enable V2 per-monitor high-DPI support for the process. This will cause
// Windows to scale dialogs, comctl32 controls, context menus, and non-client
// area owned by this process on a per-monitor basis. If per-monitor V2 is not
// available (i.e., prior to Windows 10 1703) or fails, returns false.
// https://docs.microsoft.com/en-us/windows/desktop/hidpi/dpi-awareness-context
bool EnablePerMonitorV2() {
  if (!IsUser32AndGdi32Available()) {
    return false;
  }

  static const auto set_process_dpi_awareness_context_func =
      reinterpret_cast<decltype(&::SetProcessDpiAwarenessContext)>(
          GetUser32FunctionPointer("SetProcessDpiAwarenessContext"));
  if (set_process_dpi_awareness_context_func) {
    return set_process_dpi_awareness_context_func(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }

  DCHECK_LT(GetVersion(), Version::WIN10_RS2)
      << "SetProcessDpiAwarenessContext should be available on all platforms"
         " >= Windows 10 Redstone 2";

  return false;
}

bool* GetDomainEnrollmentStateStorage() {
  static bool state = IsOS(OS_DOMAINMEMBER);
  return &state;
}

bool* GetRegisteredWithManagementStateStorage() {
  static bool state = [] {
    // Mitigate the issues caused by loading DLLs on a background thread
    // (http://crbug/973868).
    SCOPED_MAY_LOAD_LIBRARY_AT_BACKGROUND_PRIORITY();

    ScopedNativeLibrary library(
        FilePath(FILE_PATH_LITERAL("MDMRegistration.dll")));
    if (!library.is_valid()) {
      return false;
    }

    using IsDeviceRegisteredWithManagementFunction =
        decltype(&::IsDeviceRegisteredWithManagement);
    IsDeviceRegisteredWithManagementFunction
        is_device_registered_with_management_function =
            reinterpret_cast<IsDeviceRegisteredWithManagementFunction>(
                library.GetFunctionPointer("IsDeviceRegisteredWithManagement"));
    if (!is_device_registered_with_management_function) {
      return false;
    }

    BOOL is_managed = FALSE;
    HRESULT hr =
        is_device_registered_with_management_function(&is_managed, 0, nullptr);
    return SUCCEEDED(hr) && is_managed;
  }();

  return &state;
}

// TODO (crbug/1300219): return a DSREG_JOIN_TYPE* instead of bool*.
bool* GetAzureADJoinStateStorage() {
  static bool state = [] {
    base::ElapsedTimer timer;

    // Mitigate the issues caused by loading DLLs on a background thread
    // (http://crbug/973868).
    SCOPED_MAY_LOAD_LIBRARY_AT_BACKGROUND_PRIORITY();

    ScopedNativeLibrary netapi32(
        base::LoadSystemLibrary(FILE_PATH_LITERAL("netapi32.dll")));
    if (!netapi32.is_valid()) {
      return false;
    }

    const auto net_get_aad_join_information_function =
        reinterpret_cast<decltype(&::NetGetAadJoinInformation)>(
            netapi32.GetFunctionPointer("NetGetAadJoinInformation"));
    if (!net_get_aad_join_information_function) {
      return false;
    }

    const auto net_free_aad_join_information_function =
        reinterpret_cast<decltype(&::NetFreeAadJoinInformation)>(
            netapi32.GetFunctionPointer("NetFreeAadJoinInformation"));
    DPCHECK(net_free_aad_join_information_function);

    DSREG_JOIN_INFO* join_info = nullptr;
    HRESULT hr = net_get_aad_join_information_function(/*pcszTenantId=*/nullptr,
                                                       &join_info);
    const bool is_aad_joined = SUCCEEDED(hr) && join_info;
    if (join_info) {
      net_free_aad_join_information_function(join_info);
    }

    base::UmaHistogramTimes("EnterpriseCheck.AzureADJoinStatusCheckTime",
                            timer.Elapsed());
    return is_aad_joined;
  }();
  return &state;
}

NativeLibrary PinUser32Internal(NativeLibraryLoadError* error) {
  static NativeLibraryLoadError load_error;
  static const NativeLibrary user32_module =
      PinSystemLibrary(FILE_PATH_LITERAL("user32.dll"), &load_error);
  if (!user32_module && error) {
    error->code = load_error.code;
  }
  return user32_module;
}

// Returns true if the internal display, if any, is on and the device is not in
// extend mode. This allows for the correct posture decision for devices that
// are docked but either closed or in second-screen only projection mode.
bool IsValidTabletDisplayConfig() {
  uint32_t path_count;
  uint32_t mode_count;
  // All paths is expensive, therefore check only active paths.
  uint32_t flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;
  // Get the buffer sizes used for `QueryDisplayConfig()`.
  // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig
  if (::GetDisplayConfigBufferSizes(flags, &path_count, &mode_count) !=
      ERROR_SUCCESS) {
    // Default safely to Desktop in the unlikely case these calls fail.
    return false;
  }

  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);

  // Read all active paths.
  if (::QueryDisplayConfig(flags, &path_count, paths.data(), &mode_count,
                           modes.data(), nullptr) != ERROR_SUCCESS) {
    return false;
  }

  // Resize buffers to exact size from `QueryDisplayConfig()`.
  paths.resize(path_count);
  modes.resize(mode_count);

  // For each active path, check if the target points to an internal
  // display and there are no extended displays.
  // https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ne-wingdi-displayconfig_video_output_technology
  bool internal_monitor_active = false;
  bool has_extended_monitor = false;
  for (auto& path : paths) {
    if (path.sourceInfo.sourceModeInfoIdx ==
        DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID) {
      // Safely default to desktop mode if the status of an extended monitor can
      // not be determined. This prevents the browser from going into tablet
      // mode if for any reason the user is using a combination of extended and
      // duplicate displays.
      return false;
    }

    switch (path.targetInfo.outputTechnology) {
      case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
      case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
      case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
        internal_monitor_active = true;
        break;
      default:
        internal_monitor_active = false;
    }

    auto& mode = modes[path.sourceInfo.sourceModeInfoIdx];
    POINTL pos = mode.sourceMode.position;
    if ((pos.x != 0) || (pos.y != 0)) {
      has_extended_monitor = true;
      break;
    }
  }

  return internal_monitor_active && !has_extended_monitor;
}

// Check attached keyboards and populate reason string if we find ACPI\* or
// HID\VID* keyboards.
void SetKeyboardPresentOnDeviceReason(std::ostringstream& reason) {
  static constexpr GUID kKeyboardClassGuid = {
      0x4D36E96B,
      0xE325,
      0x11CE,
      {0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18}};

  // Query for all the keyboard devices.
  HDEVINFO device_info = ::SetupDiGetClassDevs(&kKeyboardClassGuid, nullptr,
                                               nullptr, DIGCF_PRESENT);
  if (device_info == INVALID_HANDLE_VALUE) {
    reason << "No keyboard info\n";
    return;
  }

  // Enumerate all keyboards and look for ACPI\PNP and HID\VID devices. If
  // the count is more than 1 we assume that a keyboard is present. This is
  // under the assumption that there will always be one keyboard device.
  for (DWORD i = 0;; ++i) {
    SP_DEVINFO_DATA device_info_data = {};
    device_info_data.cbSize = sizeof(device_info_data);
    if (!::SetupDiEnumDeviceInfo(device_info, i, &device_info_data)) {
      break;
    }

    // Get the device ID.
    wchar_t device_id[MAX_DEVICE_ID_LEN];
    CONFIGRET status = ::CM_Get_Device_ID(device_info_data.DevInst, device_id,
                                          ARRAYSIZE(device_id), 0);
    if (status == CR_SUCCESS) {
      // To reduce the scope of the hack we only look for ACPI and HID\\VID
      // prefixes in the keyboard device ids.
      if (StartsWith(device_id, L"ACPI", CompareCase::INSENSITIVE_ASCII) ||
          StartsWith(device_id, L"HID\\VID", CompareCase::INSENSITIVE_ASCII)) {
        reason << "device: ";
        reason << WideToUTF8(device_id);
        reason << '\n';
      }
    }
  }
}

bool IsWindows11TabletMode() {
  // Check if the device is convertible. Then check if the device is being
  // used as a tablet. Finally, check whether the device is in a valid
  // tablet display configuration.
  return QueryDeviceConvertibility() &&
         IsDeviceUsedAsATablet(/*reason=*/nullptr) &&
         IsValidTabletDisplayConfig();
}

// Helper for getting the process power throttling state.
ProcessPowerState GetProcessPowerThrottlingState(HANDLE process, ULONG flag) {
  // Get the current explicitly set state of the process power throttling.
  PROCESS_POWER_THROTTLING_STATE power_throttling{
      .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION};

  if (!::GetProcessInformation(process, ProcessPowerThrottling,
                               &power_throttling, sizeof(power_throttling))) {
    return ProcessPowerState::kUnset;
  }

  if (power_throttling.ControlMask & flag) {
    if (power_throttling.StateMask & flag) {
      return ProcessPowerState::kEnabled;
    }
    return ProcessPowerState::kDisabled;
  }
  return ProcessPowerState::kUnset;
}

// Helper for setting the process power throttling state.
bool SetProcessPowerThrottlingState(HANDLE process,
                                    ULONG flag,
                                    ProcessPowerState state) {
  // Process power throttling is a Windows 11 feature, but before 22H2
  // there was no way to query the current state using GetProcessInformation.
  // Getting the current state is needed in Process::GetPriority to determine
  // the process priority accurately, so calls to set the state are blocked
  // before that release.
  if (GetVersion() < Version::WIN11_22H2) {
    return false;
  }

  PROCESS_POWER_THROTTLING_STATE power_throttling{
      .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION};

  // Get the current state of the process power throttling so we do not clobber
  // other previously set flags.
  if (!::GetProcessInformation(process, ProcessPowerThrottling,
                               &power_throttling, sizeof(power_throttling))) {
    return false;
  }

  switch (state) {
    case ProcessPowerState::kUnset:
      // Clear the specified flag.  This results in the OS determining the
      // throttling state.
      power_throttling.ControlMask &= ~flag;
      power_throttling.StateMask &= ~flag;
      break;

    case ProcessPowerState::kDisabled:
      // Explicitly disable the specified flag.
      power_throttling.ControlMask |= flag;
      power_throttling.StateMask &= ~flag;
      break;

    case ProcessPowerState::kEnabled:
      // Explicitly enable the specified flag.
      power_throttling.ControlMask |= flag;
      power_throttling.StateMask |= flag;
      break;
  }

  return ::SetProcessInformation(process, ProcessPowerThrottling,
                                 &power_throttling, sizeof(power_throttling));
}

}  // namespace

// The device convertibility functions below return references to cached data
// to allow for complete test scenarios. See:
// ScopedDeviceConvertibilityStateForTesting.
//
// Returns a reference to a cached value computed on first-use that is true only
// if the device is a tablet, convertible, or detachable according to
// RtlGetDeviceFamilyInfoEnum. Looks for the following values: Tablet(2),
// Convertible(5), or Detachable(6).
// https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-deployment-deviceform
bool& IsDeviceFormConvertible() {
  static bool is_convertible = [] {
    DWORD deviceForm = DEVICEFAMILYDEVICEFORM_UNKNOWN;
    using lpfnRtlGetDeviceFamilyInfo =
        VOID(WINAPI*)(ULONGLONG*, DWORD*, DWORD*);
    static const lpfnRtlGetDeviceFamilyInfo get_device_family_info_fn =
        reinterpret_cast<lpfnRtlGetDeviceFamilyInfo>(GetProcAddress(
            ::GetModuleHandle(L"ntdll.dll"), "RtlGetDeviceFamilyInfoEnum"));
    PCHECK(get_device_family_info_fn);
    get_device_family_info_fn(/*pullUAPInfo=*/nullptr,
                              /*pulDeviceFamily=*/nullptr, &deviceForm);

    // Is not reliable for all devices. Surface Book 3 for instance has Chassis
    // Type 9 (Laptop) and DeviceForm 0 (Unknown).
    return (deviceForm == DEVICEFAMILYDEVICEFORM_TABLET) ||
           (deviceForm == DEVICEFAMILYDEVICEFORM_CONVERTIBLE) ||
           (deviceForm == DEVICEFAMILYDEVICEFORM_DETACHABLE);
  }();
  return is_convertible;
}

// Returns a reference to a cached boolean that is true if the device hardware
// is convertible. The value is determined via a WMI query for
// Win32_SystemEnclosure. This should only be executed for a small amount of
// devices that don't have ConvertibleChassis or ConvertibilityEnabled keys set.
// https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-systemenclosure
bool& IsChassisConvertible() {
  static bool chassis_convertible = [] {
    ScopedBlockingCall scoped_blocking_call(FROM_HERE, BlockingType::MAY_BLOCK);
    AssertComInitialized();

    constexpr std::wstring_view kQuery =
        L"select ChassisTypes from Win32_SystemEnclosure";
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
    if (RunWmiQuery(kCimV2ServerName, std::wstring(kQuery), &enumerator)
            .has_value()) {
      return false;
    }

    ULONG obj_count = 0;
    Microsoft::WRL::ComPtr<IWbemClassObject> info;
    HRESULT hr = E_FAIL;
    hr = enumerator->Next(25, 1, &info, &obj_count);
    if (FAILED(hr)) {
      return false;
    }

    // The accepted values for a convertible device are Tablet (30), Convertible
    // (31), and Detachable (32).
    enum ChassisType : int32_t {
      kUnknownChassisType = 2,
      kTabletChassisType = 30,
      kConvertibleChassisType = 31,
      kDetachableChassisType = 32,
    };
    int32_t chassis_type_id = kUnknownChassisType;

    if (obj_count >= 1) {
      ScopedVariant chassisTypeVariant;
      hr = info->Get(L"ChassisTypes", 0, chassisTypeVariant.Receive(), nullptr,
                     nullptr);
      if (FAILED(hr)) {
        return false;
      }

      // Although chassisType is documented as uint16[], the type in reality is
      // a 32bit integer array.
      if (chassisTypeVariant.type() == (VT_ARRAY | VT_I4)) {
        ScopedSafearray safearray(chassisTypeVariant.Release().parray);
        auto lock_scope = safearray.CreateLockScope<VT_I4>();
        if (!lock_scope) {
          return false;
        }
        chassis_type_id = (*lock_scope)[0];
      }
    }

    return (chassis_type_id == kTabletChassisType) ||
           (chassis_type_id == kConvertibleChassisType) ||
           (chassis_type_id == kDetachableChassisType);
  }();
  return chassis_convertible;
}

// Returns a reference to a cached boolean optional. If a value exists, it means
// that the queried registry key, ConvertibilityEnabled, exists. Used by Surface
// for devices that can't set deviceForm or ChassisType. The RegKey need not
// exist, but if it does it will override other checks.
std::optional<bool>& GetConvertibilityEnabledOverride() {
  static std::optional<bool> convertibility_enabled =
      []() -> std::optional<bool> {
    DWORD data;
    base::win::RegKey key(
        HKEY_LOCAL_MACHINE,
        L"System\\CurrentControlSet\\Control\\PriorityControl",
        KEY_QUERY_VALUE);
    return key.ReadValueDW(L"ConvertibilityEnabled", &data) == ERROR_SUCCESS
               ? std::make_optional(data != 0)
               : std::nullopt;
  }();
  return convertibility_enabled;
}

// Returns a reference to a cached boolean optional. If a value exists, it means
// that the queried registry key, ConvertibleChassis, exists. Windows may cache
// the results of convertible chassis queries, preventing the need for running
// the expensive WMI query. This should always be checked prior to running
// `IsChassisConvertible()`.
std::optional<bool>& GetConvertibleChassisKeyValue() {
  static std::optional<bool> convertible_chassis = []() -> std::optional<bool> {
    DWORD data;
    base::win::RegKey key(HKEY_CURRENT_USER,
                          L"SOFTWARE\\Microsoft\\TabletTip\\ConvertibleChassis",
                          KEY_QUERY_VALUE);
    return key.ReadValueDW(L"ConvertibleChassis", &data) == ERROR_SUCCESS
               ? std::make_optional(data != 0)
               : std::nullopt;
  }();
  return convertible_chassis;
}

// Returns a function pointer that points to the lambda function that
// tracks if the device's convertible slate mode state has ever changed, which
// would indicate that proper GPIO drivers are available for a convertible
// machine. A pointer is used so that the function at the address can be
// replaced for testing purposes.
QueryKeyFunction& HasCSMStateChanged() {
  static QueryKeyFunction state = []() {
    DWORD data;
    base::win::RegKey key(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\TabletTip\\ConvertibleSlateModeChanged",
        KEY_QUERY_VALUE);
    bool value_exists =
        key.ReadValueDW(L"ConvertibleSlateModeChanged", &data) == ERROR_SUCCESS;
    return (value_exists && data != 0);
  };
  return state;
}

void IsDeviceInTabletMode(HWND hwnd, OnceCallback<void(bool)> callback) {
  if (GetVersion() >= Version::WIN11) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&IsWindows11TabletMode), std::move(callback));
    return;
  }
  SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, BindOnce(std::move(callback), IsWindows10TabletMode(hwnd)));
}

bool QueryDeviceConvertibility() {
  // Ensure this function runs on a thread that allows blocking in the event
  // that the WMI query in the chassis convertibility check is executed.
  AssertBlockingAllowed();

  if (const auto& convertibility_enabled = GetConvertibilityEnabledOverride()) {
    return *convertibility_enabled;
  }

  if (const auto& convertible_chassis_key = GetConvertibleChassisKeyValue()) {
    return *convertible_chassis_key;
  }

  if (IsDeviceFormConvertible() || IsChassisConvertible()) {
    return true;
  }

  return (HasCSMStateChanged())();
}

// Uses the Windows 10 WRL API's to query the current system state. The APIs
// used in the function below are supported in Win32 apps as per msdn.
// It looks like the API implementation is buggy at least on Surface 4 causing
// it to always return UserInteractionMode_Touch which as per documentation
// indicates tablet mode.
bool IsWindows10TabletMode(HWND hwnd) {
  ScopedHString view_settings_guid = ScopedHString::Create(
      RuntimeClass_Windows_UI_ViewManagement_UIViewSettings);
  Microsoft::WRL::ComPtr<IUIViewSettingsInterop> view_settings_interop;
  HRESULT hr = ::RoGetActivationFactory(view_settings_guid.get(),
                                        IID_PPV_ARGS(&view_settings_interop));
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IUIViewSettings>
      view_settings;
  hr = view_settings_interop->GetForWindow(hwnd, IID_PPV_ARGS(&view_settings));
  if (FAILED(hr)) {
    return false;
  }

  ABI::Windows::UI::ViewManagement::UserInteractionMode mode =
      ABI::Windows::UI::ViewManagement::UserInteractionMode_Mouse;
  hr = view_settings->get_UserInteractionMode(&mode);
  if (FAILED(hr)) {
    return false;
  }

  return mode == ABI::Windows::UI::ViewManagement::UserInteractionMode_Touch;
}

void IsDeviceSlateWithKeyboard(HWND hwnd,
                               OnceCallback<void(bool, std::string)> callback) {
  std::ostringstream reason;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableUsbKeyboardDetect)) {
    reason << "Detection disabled";
    SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, BindOnce(std::move(callback), /*keyboard_present=*/false,
                            std::move(reason).str()));
    return;
  }

  // If no touch screen detected, assume keyboard attached.
  // TODO(crbug.com/383267933) If the device is mouse only with no touch screen,
  // this will determine that the device has a keyboard.
  if ((GetSystemMetrics(SM_DIGITIZER) & NID_INTEGRATED_TOUCH) !=
      NID_INTEGRATED_TOUCH) {
    reason << "NID_INTEGRATED_TOUCH\n";
  }

  // Once the async call to IsDeviceInTabletMode is complete, run
  // `on_tablet_mode_determined` to populate the `reason` string if present and
  // run the `callback`.
  auto on_tablet_mode_determined =
      [](OnceCallback<void(bool, std::string)> keyboard_callback,
         std::ostringstream reason, bool is_tablet_mode) {
        if (is_tablet_mode) {
          reason << "Tablet device.\n";

          std::move(keyboard_callback)
              .Run(/*keyboard_present=*/false, std::move(reason).str());
          return;
        }

        reason << "Not a tablet device.\n";
        SetKeyboardPresentOnDeviceReason(reason);
        std::move(keyboard_callback)
            .Run(/*keyboard_present=*/true, std::move(reason).str());
      };

  IsDeviceInTabletMode(hwnd,
                       base::BindOnce(std::move(on_tablet_mode_determined),
                                      std::move(callback), std::move(reason)));
}

static bool g_crash_on_process_detach = false;

bool GetUserSidString(std::wstring* user_sid) {
  std::optional<AccessToken> token = AccessToken::FromCurrentProcess();
  if (!token) {
    return false;
  }
  std::optional<std::wstring> sid_string = token->User().ToSddlString();
  if (!sid_string) {
    return false;
  }
  *user_sid = *sid_string;
  return true;
}

class ScopedAllowBlockingForUserAccountControl : public ScopedAllowBlocking {};

bool UserAccountControlIsEnabled() {
  // This can be slow if Windows ends up going to disk.  Should watch this key
  // for changes and only read it once, preferably on the file thread.
  //   http://code.google.com/p/chromium/issues/detail?id=61644
  ScopedAllowBlockingForUserAccountControl allow_blocking;

  RegKey key(HKEY_LOCAL_MACHINE,
             L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
             KEY_READ);
  DWORD uac_enabled;
  if (key.ReadValueDW(L"EnableLUA", &uac_enabled) != ERROR_SUCCESS) {
    return true;
  }
  // Users can set the EnableLUA value to something arbitrary, like 2, which
  // Vista will treat as UAC enabled, so we make sure it is not set to 0.
  return (uac_enabled != 0);
}

bool SetBooleanValueForPropertyStore(IPropertyStore* property_store,
                                     const PROPERTYKEY& property_key,
                                     bool property_bool_value) {
  ScopedPropVariant property_value;
  if (FAILED(InitPropVariantFromBoolean(property_bool_value,
                                        property_value.Receive()))) {
    return false;
  }

  return SetPropVariantValueForPropertyStore(property_store, property_key,
                                             property_value);
}

bool SetStringValueForPropertyStore(IPropertyStore* property_store,
                                    const PROPERTYKEY& property_key,
                                    const wchar_t* property_string_value) {
  ScopedPropVariant property_value;
  if (FAILED(InitPropVariantFromString(property_string_value,
                                       property_value.Receive()))) {
    return false;
  }

  return SetPropVariantValueForPropertyStore(property_store, property_key,
                                             property_value);
}

bool SetClsidForPropertyStore(IPropertyStore* property_store,
                              const PROPERTYKEY& property_key,
                              const CLSID& property_clsid_value) {
  ScopedPropVariant property_value;
  if (FAILED(InitPropVariantFromCLSID(property_clsid_value,
                                      property_value.Receive()))) {
    return false;
  }

  return SetPropVariantValueForPropertyStore(property_store, property_key,
                                             property_value);
}

bool SetAppIdForPropertyStore(IPropertyStore* property_store,
                              const wchar_t* app_id) {
  // App id should be less than 128 chars and contain no space. And recommended
  // format is CompanyName.ProductName[.SubProduct.ProductNumber].
  // See
  // https://docs.microsoft.com/en-us/windows/win32/shell/appids#how-to-form-an-application-defined-appusermodelid
  DCHECK_LT(lstrlen(app_id), 128);
  DCHECK_EQ(wcschr(app_id, L' '), nullptr);

  return SetStringValueForPropertyStore(property_store, PKEY_AppUserModel_ID,
                                        app_id);
}

static const wchar_t kAutoRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool AddCommandToAutoRun(HKEY root_key,
                         const std::wstring& name,
                         const std::wstring& command) {
  RegKey autorun_key(root_key, kAutoRunKeyPath, KEY_SET_VALUE);
  return (autorun_key.WriteValue(name.c_str(), command.c_str()) ==
          ERROR_SUCCESS);
}

bool RemoveCommandFromAutoRun(HKEY root_key, const std::wstring& name) {
  RegKey autorun_key(root_key, kAutoRunKeyPath, KEY_SET_VALUE);
  return (autorun_key.DeleteValue(name.c_str()) == ERROR_SUCCESS);
}

bool ReadCommandFromAutoRun(HKEY root_key,
                            const std::wstring& name,
                            std::wstring* command) {
  RegKey autorun_key(root_key, kAutoRunKeyPath, KEY_QUERY_VALUE);
  return (autorun_key.ReadValue(name.c_str(), command) == ERROR_SUCCESS);
}

void SetShouldCrashOnProcessDetach(bool crash) {
  g_crash_on_process_detach = crash;
}

bool ShouldCrashOnProcessDetach() {
  return g_crash_on_process_detach;
}

void SetAbortBehaviorForCrashReporting() {
  // Prevent CRT's abort code from prompting a dialog or trying to "report" it.
  // Disabling the _CALL_REPORTFAULT behavior is important since otherwise it
  // has the sideffect of clearing our exception filter, which means we
  // don't get any crash.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

  // Set a SIGABRT handler for good measure. We will crash even if the default
  // is left in place, however this allows us to crash earlier. And it also
  // lets us crash in response to code which might directly call raise(SIGABRT)
  signal(SIGABRT, ForceCrashOnSigAbort);
}

// This method is used to set the right interactions media queries,
// see https://drafts.csswg.org/mediaqueries-4/#mf-interaction. It doesn't
// check the Windows 10 tablet mode because it doesn't reflect the actual
// input configuration of the device and can be manually triggered by the user
// independently from the hardware state.
bool IsDeviceUsedAsATablet(std::string* reason) {
  // Once this is set, it shouldn't be overridden, and it should be the ultimate
  // return value, so that this method returns the same result whether or not
  // reason is NULL.
  std::optional<bool> ret;

  if (GetSystemMetrics(SM_MAXIMUMTOUCHES) == 0) {
    if (!reason) {
      return false;
    }

    *reason += "Device does not support touch.\n";
    ret = false;
  }

  // If the device is docked, the user is treating the device as a PC.
  if (GetSystemMetrics(SM_SYSTEMDOCKED) != 0) {
    if (!reason) {
      return false;
    }

    *reason += "SM_SYSTEMDOCKED\n";
    if (!ret.has_value()) {
      ret = false;
    }
  }

  // If the device is not supporting rotation, it's unlikely to be a tablet,
  // a convertible or a detachable.
  // See
  // https://msdn.microsoft.com/en-us/library/windows/desktop/dn629263(v=vs.85).aspx
  using GetAutoRotationStateType = decltype(GetAutoRotationState)*;
  static const auto get_auto_rotation_state_func =
      reinterpret_cast<GetAutoRotationStateType>(
          GetUser32FunctionPointer("GetAutoRotationState"));
  if (get_auto_rotation_state_func) {
    AR_STATE rotation_state = AR_ENABLED;
    if (get_auto_rotation_state_func(&rotation_state) &&
        (rotation_state & (AR_NOT_SUPPORTED | AR_LAPTOP | AR_NOSENSOR)) != 0) {
      return ret.value_or(false);
    }
  }

  // PlatformRoleSlate was added in Windows 8+.
  POWER_PLATFORM_ROLE role = GetPlatformRole();
  bool is_tablet = false;
  if (role == PlatformRoleMobile || role == PlatformRoleSlate) {
    is_tablet = !GetSystemMetrics(SM_CONVERTIBLESLATEMODE);
    if (!is_tablet) {
      if (!reason) {
        return false;
      }

      *reason += "Not in slate mode.\n";
      if (!ret.has_value()) {
        ret = false;
      }
    } else if (reason) {
      *reason += (role == PlatformRoleMobile) ? "PlatformRoleMobile\n"
                                              : "PlatformRoleSlate\n";
    }
  } else if (reason) {
    *reason += "Device role is not mobile or slate.\n";
  }
  return ret.value_or(is_tablet);
}

bool IsEnrolledToDomain() {
  return *GetDomainEnrollmentStateStorage();
}

bool IsDeviceRegisteredWithManagement() {
  // GetRegisteredWithManagementStateStorage() can be true for devices running
  // the Home sku, however the Home sku does not allow for management of the web
  // browser. As such, we automatically exclude devices running the Home sku.
  if (OSInfo::GetInstance()->version_type() == SUITE_HOME) {
    return false;
  }
  return *GetRegisteredWithManagementStateStorage();
}

bool IsJoinedToAzureAD() {
  return *GetAzureADJoinStateStorage();
}

bool IsUser32AndGdi32Available() {
  static const bool is_user32_and_gdi32_available = [] {
    // If win32k syscalls aren't disabled, then user32 and gdi32 are available.
    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY policy = {};
    if (::GetProcessMitigationPolicy(GetCurrentProcess(),
                                     ProcessSystemCallDisablePolicy, &policy,
                                     sizeof(policy))) {
      return policy.DisallowWin32kSystemCalls == 0;
    }

    return true;
  }();
  return is_user32_and_gdi32_available;
}

bool GetLoadedModulesSnapshot(HANDLE process, std::vector<HMODULE>* snapshot) {
  DCHECK(snapshot);
  DCHECK_EQ(0u, snapshot->size());
  snapshot->resize(128);

  // We will retry at least once after first determining |bytes_required|. If
  // the list of modules changes after we receive |bytes_required| we may retry
  // more than once.
  int retries_remaining = 5;
  do {
    DWORD bytes_required = 0;
    // EnumProcessModules returns 'success' even if the buffer size is too
    // small.
    DCHECK_GE(std::numeric_limits<DWORD>::max(),
              snapshot->size() * sizeof(HMODULE));
    if (!::EnumProcessModules(
            process, &(*snapshot)[0],
            static_cast<DWORD>(snapshot->size() * sizeof(HMODULE)),
            &bytes_required)) {
      DPLOG(ERROR) << "::EnumProcessModules failed.";
      return false;
    }

    DCHECK_EQ(0u, bytes_required % sizeof(HMODULE));
    size_t num_modules = bytes_required / sizeof(HMODULE);
    if (num_modules <= snapshot->size()) {
      // Buffer size was too big, presumably because a module was unloaded.
      snapshot->erase(snapshot->begin() + static_cast<ptrdiff_t>(num_modules),
                      snapshot->end());
      return true;
    }

    if (num_modules == 0) {
      DLOG(ERROR) << "Can't determine the module list size.";
      return false;
    }

    // Buffer size was too small. Try again with a larger buffer. A little
    // more room is given to avoid multiple expensive calls to
    // ::EnumProcessModules() just because one module has been added.
    snapshot->resize(num_modules + 8, nullptr);
  } while (--retries_remaining);

  DLOG(ERROR) << "Failed to enumerate modules.";
  return false;
}

void EnableFlicks(HWND hwnd) {
  ::RemoveProp(hwnd, MICROSOFT_TABLETPENSERVICE_PROPERTY);
}

void DisableFlicks(HWND hwnd) {
  ::SetProp(hwnd, MICROSOFT_TABLETPENSERVICE_PROPERTY,
            reinterpret_cast<HANDLE>(TABLET_DISABLE_FLICKS |
                                     TABLET_DISABLE_FLICKFALLBACKKEYS));
}

void EnableHighDPISupport() {
  if (!IsUser32AndGdi32Available()) {
    return;
  }

  // Enable per-monitor V2 if it is available (Win10 1703 or later).
  if (EnablePerMonitorV2()) {
    return;
  }

  // Fall back to per-monitor DPI for older versions of Win10.
  PROCESS_DPI_AWARENESS process_dpi_awareness = PROCESS_PER_MONITOR_DPI_AWARE;
  if (!::SetProcessDpiAwareness(process_dpi_awareness)) {
    // For windows versions where SetProcessDpiAwareness fails, try its
    // predecessor.
    BOOL result = ::SetProcessDPIAware();
    DCHECK(result) << "SetProcessDPIAware failed.";
  }
}

std::wstring WStringFromGUID(const ::GUID& rguid) {
  // This constant counts the number of characters in the formatted string,
  // including the null termination character.
  constexpr int kGuidStringCharacters =
      1 + 8 + 1 + 4 + 1 + 4 + 1 + 4 + 1 + 12 + 1 + 1;
  wchar_t guid_string[kGuidStringCharacters];
  CHECK(SUCCEEDED(StringCchPrintfW(
      guid_string, kGuidStringCharacters,
      L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", rguid.Data1,
      rguid.Data2, rguid.Data3, rguid.Data4[0], rguid.Data4[1], rguid.Data4[2],
      rguid.Data4[3], rguid.Data4[4], rguid.Data4[5], rguid.Data4[6],
      rguid.Data4[7])));
  return std::wstring(guid_string, kGuidStringCharacters - 1);
}

bool PinUser32(NativeLibraryLoadError* error) {
  return PinUser32Internal(error) != nullptr;
}

void* GetUser32FunctionPointer(const char* function_name,
                               NativeLibraryLoadError* error) {
  NativeLibrary user32_module = PinUser32Internal(error);
  if (user32_module) {
    return GetFunctionPointerFromNativeLibrary(user32_module, function_name);
  }
  return nullptr;
}

std::wstring GetWindowObjectName(HANDLE handle) {
  // Get the size of the name.
  std::wstring object_name;

  DWORD size = 0;
  ::GetUserObjectInformation(handle, UOI_NAME, nullptr, 0, &size);
  if (!size) {
    DPCHECK(false);
    return object_name;
  }

  LOG_ASSERT(size % sizeof(wchar_t) == 0u);

  // Query the name of the object.
  if (!::GetUserObjectInformation(
          handle, UOI_NAME, WriteInto(&object_name, size / sizeof(wchar_t)),
          size, &size)) {
    DPCHECK(false);
  }

  return object_name;
}

bool GetPointerDevice(HANDLE device, POINTER_DEVICE_INFO& result) {
  return ::GetPointerDevice(device, &result);
}

std::optional<std::vector<POINTER_DEVICE_INFO>> GetPointerDevices() {
  uint32_t device_count;
  if (!::GetPointerDevices(&device_count, nullptr)) {
    return std::nullopt;
  }

  std::vector<POINTER_DEVICE_INFO> pointer_devices(device_count);
  if (!::GetPointerDevices(&device_count, pointer_devices.data())) {
    return std::nullopt;
  }
  return pointer_devices;
}

bool RegisterPointerDeviceNotifications(HWND hwnd,
                                        bool notify_proximity_changes) {
  return ::RegisterPointerDeviceNotifications(hwnd, notify_proximity_changes);
}

bool IsRunningUnderDesktopName(std::wstring_view desktop_name) {
  HDESK thread_desktop = ::GetThreadDesktop(::GetCurrentThreadId());
  if (!thread_desktop) {
    return false;
  }

  std::wstring current_desktop_name = GetWindowObjectName(thread_desktop);
  return EqualsCaseInsensitiveASCII(AsStringPiece16(current_desktop_name),
                                    AsStringPiece16(desktop_name));
}

// This method is used to detect whether current session is a remote session.
// See:
// https://docs.microsoft.com/en-us/windows/desktop/TermServ/detecting-the-terminal-services-environment
bool IsCurrentSessionRemote() {
  if (::GetSystemMetrics(SM_REMOTESESSION)) {
    return true;
  }

  DWORD current_session_id = 0;

  if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &current_session_id)) {
    return false;
  }

  static constexpr wchar_t kRdpSettingsKeyName[] =
      L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server";
  RegKey key(HKEY_LOCAL_MACHINE, kRdpSettingsKeyName, KEY_READ);
  if (!key.Valid()) {
    return false;
  }

  static constexpr wchar_t kGlassSessionIdValueName[] = L"GlassSessionId";
  DWORD glass_session_id = 0;
  if (key.ReadValueDW(kGlassSessionIdValueName, &glass_session_id) !=
      ERROR_SUCCESS) {
    return false;
  }

  return current_session_id != glass_session_id;
}

bool IsAppVerifierLoaded() {
  return GetModuleHandleA(kApplicationVerifierDllName);
}

std::optional<std::wstring> ExpandEnvironmentVariables(wcstring_view str) {
  std::wstring path_expanded;
  DWORD path_len = MAX_PATH;
  for (int iterations = 0; iterations < 5; iterations++) {
    DWORD result = ::ExpandEnvironmentStringsW(
        str.c_str(), base::WriteInto(&path_expanded, path_len), path_len);
    if (!result) {
      // Failed to expand variables.
      break;
    }
    if (result <= path_len) {
      return path_expanded.substr(0, result - 1);
    }
    path_len = result;
  }

  return std::nullopt;
}

expected<std::wstring, NTSTATUS> GetObjectTypeName(HANDLE handle) {
  if (!HandleTraits::IsHandleValid(handle)) {
    return unexpected(STATUS_INVALID_HANDLE);
  }

  // The buffer must be large enough to hold the type info struct plus the type
  // string and its terminator. Allocate a buffer large enough to hold a type
  // name of 31 characters. This is far larger than the 13 chars needed for
  // "WindowStation".
  static constexpr size_t kMaxTypeNameLength = 31;
  static constexpr size_t kBufferSize =
      sizeof(PUBLIC_OBJECT_TYPE_INFORMATION) +
      (kMaxTypeNameLength + 1) * sizeof(wchar_t);
  auto buffer = HeapArray<uint8_t>::Uninit(kBufferSize);
  auto* type_info =
      reinterpret_cast<PUBLIC_OBJECT_TYPE_INFORMATION*>(buffer.data());
  ULONG type_info_length = 0;
  if (auto status =
          ::NtQueryObject(handle, ObjectTypeInformation, type_info,
                          static_cast<ULONG>(buffer.size()), &type_info_length);
      status != STATUS_SUCCESS) {
    if (status == STATUS_INFO_LENGTH_MISMATCH) {
      // The call should never fail due to lack of space in the buffer as per
      // calculations above. Report the required size in this case so that the
      // calculations can be revised.
      SCOPED_CRASH_KEY_NUMBER("NtQueryObject", "type_info_length",
                              type_info_length);
      debug::DumpWithoutCrashing();  // https://crbug.com/40071993.
    }
    return unexpected(status);
  }
  return std::wstring(type_info->TypeName.Buffer,
                      type_info->TypeName.Length / sizeof(wchar_t));
}

expected<ScopedHandle, NTSTATUS> TakeHandleOfType(
    HANDLE handle,
    std::wstring_view object_type_name) {
  auto type_name = GetObjectTypeName(handle);
  if (!type_name.has_value()) {
    // `handle` is invalid. Return the error to the caller.
    return unexpected(type_name.error());
  }
  // Crash if `handle` is an unexpected type. This represents a dangerous
  // type confusion condition that should never happen.
  base::debug::Alias(&handle);
  CHECK_EQ(*type_name, object_type_name);
  return ScopedHandle(handle);  // Ownership of `handle` goes to the caller.
}

ProcessPowerState GetProcessEcoQoSState(HANDLE process) {
  return GetProcessPowerThrottlingState(
      process, PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
}

ProcessPowerState GetProcessTimerThrottleState(HANDLE process) {
  return GetProcessPowerThrottlingState(
      process, PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION);
}

bool SetProcessEcoQoSState(HANDLE process, ProcessPowerState state) {
  return SetProcessPowerThrottlingState(
      process, PROCESS_POWER_THROTTLING_EXECUTION_SPEED, state);
}

bool SetProcessTimerThrottleState(HANDLE process, ProcessPowerState state) {
  return SetProcessPowerThrottlingState(
      process, PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION, state);
}

ScopedDomainStateForTesting::ScopedDomainStateForTesting(bool state)
    : initial_state_(IsEnrolledToDomain()) {
  *GetDomainEnrollmentStateStorage() = state;
}

ScopedDomainStateForTesting::~ScopedDomainStateForTesting() {
  *GetDomainEnrollmentStateStorage() = initial_state_;
}

ScopedDeviceRegisteredWithManagementForTesting::
    ScopedDeviceRegisteredWithManagementForTesting(bool state)
    : initial_state_(IsDeviceRegisteredWithManagement()) {
  *GetRegisteredWithManagementStateStorage() = state;
}

ScopedDeviceRegisteredWithManagementForTesting::
    ~ScopedDeviceRegisteredWithManagementForTesting() {
  *GetRegisteredWithManagementStateStorage() = initial_state_;
}

ScopedAzureADJoinStateForTesting::ScopedAzureADJoinStateForTesting(bool state)
    : initial_state_(std::exchange(*GetAzureADJoinStateStorage(), state)) {}

ScopedAzureADJoinStateForTesting::~ScopedAzureADJoinStateForTesting() {
  *GetAzureADJoinStateStorage() = initial_state_;
}

ScopedDeviceConvertibilityStateForTesting::
    ScopedDeviceConvertibilityStateForTesting(
        bool form_convertible,
        bool chassis_convertible,
        QueryKeyFunction csm_changed,
        std::optional<bool> convertible_chassis_key,
        std::optional<bool> convertibility_enabled)
    : initial_form_convertible_(&IsDeviceFormConvertible(), form_convertible),
      initial_chassis_convertible_(&IsChassisConvertible(),
                                   chassis_convertible),
      initial_csm_changed_(&HasCSMStateChanged(), csm_changed),
      initial_convertible_chassis_key_(&GetConvertibleChassisKeyValue(),
                                       convertible_chassis_key),
      initial_convertibility_enabled_(&GetConvertibilityEnabledOverride(),
                                      convertibility_enabled) {}

ScopedDeviceConvertibilityStateForTesting::
    ~ScopedDeviceConvertibilityStateForTesting() = default;

}  // namespace win
}  // namespace base
