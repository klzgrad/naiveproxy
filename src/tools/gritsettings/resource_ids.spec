# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This file is used to assign starting resource ids for resources and strings
# used by Chromium.  This is done to ensure that resource ids are unique
# across all the grd files.  If you are adding a new grd file, please add
# a new entry to this file.
#
# The entries below are organized into sections. When adding new entries,
# please use the right section. Try to keep entries in alphabetical order.
#
# - chrome/app/
# - chrome/browser/
# - chrome/ WebUI
# - chrome/ miscellaneous
# - chromeos/
# - components/
# - ios/ (overlaps with chrome/)
# - content/
# - everything else
#
# The range of ID values, which is used by pak files, is from 0 to 2^16 - 1.
#
# IMPORTANT: For update instructions, see README.md.
{
  # The first entry in the file, SRCDIR, is special: It is a relative path from
  # this file to the base of your checkout.
  "SRCDIR": "../..",

  # START chrome/app section.
  #
  # chrome/ and ios/chrome/ must start at the same id.
  # App only use one file depending on whether it is iOS or other platform.
  # Chromium strings and Google Chrome strings must start at the same id.
  # We only use one file depending on whether we're building Chromium or
  # Google Chrome.
  "chrome/app/chromium_strings.grd": {
    "messages": [800],
  },
  "chrome/app/google_chrome_strings.grd": {
    "messages": [800],
  },

  # Leave lots of space for generated_resources since it has most of our
  # strings.
  "chrome/app/generated_resources.grd": {
    # Big alignment since strings (previous item) are frequently added.
    "META": {"join": 2, "align": 200},
    "messages": [1000],
  },

  "chrome/app/resources/locale_settings.grd": {
    # Big alignment since strings (previous item) are frequently added.
    "META": {"align": 1000},
    "messages": [2000],
  },

  # These each start with the same resource id because we only use one
  # file for each build (chromiumos, google_chromeos, linux, mac, or win).
  "chrome/app/resources/locale_settings_chromiumos.grd": {
    # Big alignment since strings (previous item) are frequently added.
    "META": {"align": 100},
    "messages": [2100],
  },
  "chrome/app/resources/locale_settings_google_chromeos.grd": {
    "messages": [2100],
  },
  "chrome/app/resources/locale_settings_linux.grd": {
    "messages": [2100],
  },
  "chrome/app/resources/locale_settings_mac.grd": {
    "messages": [2100],
  },
  "chrome/app/resources/locale_settings_win.grd": {
    "messages": [2100],
  },

  "chrome/app/theme/chrome_unscaled_resources.grd": {
    "META": {"join": 5},
    "includes": [2120],
  },
  "chrome/app/theme/google_chrome/chromeos/chromeos_chrome_internal_strings.grd": {
    "messages": [2140],
  },

  # Leave space for theme_resources since it has many structures.
  "chrome/app/theme/theme_resources.grd": {
    "structures": [2160],
  },
  # END chrome/app section.

  # START chrome/browser section.
  "chrome/browser/browser_resources.grd": {
    # Big alignment at start of section.
    "META": {"align": 100},
    "includes": [2200],
    "structures": [2220],
  },
  "chrome/browser/dev_ui_browser_resources.grd": {
    "includes": [2240],
  },
  "chrome/browser/nearby_sharing/internal/nearby_share_internal_icons.grd": {
    "includes": [2260],
  },
  "chrome/browser/nearby_sharing/internal/nearby_share_internal_strings.grd": {
    "messages": [2280],
  },
  "chrome/browser/platform_experience/win/resources/platform_experience_win_resources.grd": {
    "includes": [2300],
    "messages": [2320],
  },
  "chrome/browser/recent_tabs/internal/android/java/strings/android_restore_tabs_strings.grd": {
    "messages": [2340],
  },
  "chrome/browser/resources/app_icon/app_icon_resources.grd": {
    "structures": [2360],
  },
  "chrome/browser/resources/chromeos/app_icon/app_icon_resources.grd": {
    "structures": [2380],
  },
  "chrome/browser/resources/chromeos/mako/resources.grd": {
    "META": {"sizes": {"includes": [150]}},
    "includes": [2400],
  },
  "chrome/browser/resources/chromeos/seal/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [2420],
  },
  "chrome/browser/resources/component_extension_resources.grd": {
    "includes": [2440],
    "structures": [2460],
  },
  "chrome/browser/resources/office_web_app/resources.grd": {
    "includes": [2480],
  },
  "chrome/browser/resources/preinstalled_web_apps/resources.grd": {
    "includes": [2500],
  },
  "chrome/browser/test_dummy/internal/android/resources/resources.grd": {
    "includes": [2520],
  },
  # chrome/browser/glic/resources/internal/browser_resources.grd and
  # chrome/browser/glic/resources/browser_resources.grd must share the same id
  # because they define the same resources, but only one of them is built
  # depending on whether src_internal is available.
  "chrome/browser/glic/resources/internal/browser_resources.grd": {
    "messages": [2540],
    "includes": [2600],
  },
  "chrome/browser/glic/resources/browser_resources.grd": {
    "messages": [2540],
    "includes": [2600],
  },
  # END chrome/browser section.

  # START chrome/ WebUI resources section
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/about_sys/resources.grd": {
    # Big alignment at start of section.
    "META": {"align": 100, "sizes": {"includes": [10]}},
    "includes": [2700],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/access_code_cast/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [2720],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/accessibility/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [2740],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/app_home/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [2760],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/app_service_internals/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [2780],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/app_settings/resources.grd": {
    "META": {"sizes": {"includes": [45]}},
    "includes": [2800],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/ash/extended_updates/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [2820],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/ash/inline_login/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [2840],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/ash/print_preview/resources.grd": {
    "META": {"sizes": {"includes": [500]}},
    "includes": [2850],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/ash/settings/resources.grd": {
    "META": {"sizes": {"includes": [1000],}},
    "includes": [2860],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/bluetooth_internals/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [2880],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/bookmarks/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [2900],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/browser_switch/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [2920],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/browsing_topics/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [2940],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/certificate_viewer/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [2960],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/add_supervision/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [2980],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/app_install/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [3000],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/assistant_optin/assistant_optin_resources.grd": {
    "META": {"sizes": {"includes": [80]}},
    "includes": [3040],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/audio/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [3060],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/bluetooth_pairing_dialog/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3080],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/borealis_installer/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [3100],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/cloud_upload/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [3120],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/crostini_installer/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3130],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/crostini_upgrader/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3135],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/desk_api/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3140],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/edu_coexistence/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [3160],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/emoji_picker/resources.grd": {
    "META": {"sizes": {"includes": [60]}},
    "includes": [3180],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/enterprise_reporting/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [3200],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/gaia_action_buttons/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3220],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/healthd_internals/resources.grd": {
    "META": {"sizes": {"includes": [70]}},
    "includes": [3240],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/internet_config_dialog/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3260],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/internet_detail_dialog/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3280],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/kerberos/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [3300],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/launcher_internals/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [3320],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/lock_screen_reauth/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [3340],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/login/resources.grd": {
    "META": {"sizes": {"includes": [320],}},
    "includes": [3360],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/manage_mirrorsync/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3380],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/multidevice_internals/resources.grd": {
    "META": {"sizes": {"includes": [35]}},
    "includes": [3400],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/multidevice_setup/multidevice_setup_resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3420],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/nearby_internals/resources.grd": {
    "META": {"sizes": {"includes": [40]}},
    "includes": [3460],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/nearby_share/resources.grd": {
    "META": {"sizes": {"includes": [100]}},
    "includes": [3480],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/network_ui/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [3500],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/notification_tester/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [3520],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/parent_access/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [3540],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/password_change/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [3560],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/remote_maintenance_curtain/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3580],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/sensor_info/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [3600],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/set_time_dialog/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [3620],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/skyvault/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3640],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/supervision/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3660],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/vm/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [3690],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/commerce/product_specifications/resources.grd": {
    "META": {"sizes": {"includes": [70]}},
    "includes": [3700],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/components/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [3720],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/compose/resources.grd": {
    "META": {"sizes": {"includes": [15]}},
    "includes": [3740],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/connectors_internals/resources.grd": {
    "META": {"sizes": {"includes": [15]}},
    "includes": [3760],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/data_sharing/resources.grd": {
   "META": {"sizes": {"includes": [20]}},
    "includes": [3780],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/data_sharing_internals/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [3800],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/device_log/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [3820],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/discards/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [3840],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/chromeos/dlp_internals/resources.grd": {
    "META": {"sizes": {"includes": [15]}},
    "includes": [3860],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/downloads/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [3880],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/engagement/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [3900],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/extensions/resources.grd": {
    "META": {"sizes": {"includes": [120],}},
    "includes": [3920],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/family_link_user_internals/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [3930],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/feed/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [3940],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/feed_internals/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [3960],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/feedback/resources.grd": {
    "META": {"sizes": {"includes": [30],}},
    "includes": [3980],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/gaia_auth_host/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [4000],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/glic/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [4010],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/glic/fre/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [4020],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/history/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [4040],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/inline_login/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4080],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/inspect/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [4085],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/invalidations/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [4100],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/intro/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [4140],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/key_value_pair_viewer_shared/resources.grd": {
   "META": {"sizes": {"includes": [10]}},
    "includes": [4160],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/lens/overlay/resources.grd": {
    "META": {"sizes": {"includes": [70]}},
    "includes": [4180],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/lens/shared/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [4240],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/location_internals/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [4260],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/management/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4280],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/media/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [4300],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/media_router/cast_feedback/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [4320],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/media_router/internals/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4340],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/memory_internals/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [4360],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/net_internals/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [4380],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/new_tab_page/resources.grd": {
    "META": {"sizes": {"includes": [200]}},
    "includes": [4400],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/new_tab_page/untrusted/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [4410],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/new_tab_page_instant/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4420],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/new_tab_page_third_party/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4440],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/omnibox/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [4460],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/omnibox_popup/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [4480],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/on_device_internals/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [4500],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/on_device_translation_internals/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [4510],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/password_manager/resources.grd": {
    "META": {"sizes": {"includes": [200]}},
    "includes": [4520],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/pdf/resources.grd": {
    "META": {"sizes": {"includes": [200]}},
    "includes": [4540],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/predictors/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [4560],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/print_preview/resources.grd": {
    "META": {"sizes": {"includes": [500],}},
    "includes": [4580],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/privacy_sandbox/internals/resources.grd": {
   "META": {"sizes": {"includes": [80],}},
    "includes": [4600],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/privacy_sandbox/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [4620],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/profile_internals/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [4640],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/quota_internals/quota_internals_resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [4660],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/sandbox_internals/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [4680],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/saved_tab_groups_unsupported/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [4690],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/search_engine_choice/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [4700],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/segmentation_internals/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4720],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/settings/resources.grd": {
    "META": {"sizes": {"includes": [500],}},
    "includes": [4740],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/settings_shared/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [4760],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/bookmarks/resources.grd": {
    "META": {"sizes": {"includes": [45],}},
    "includes": [4780],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/commerce/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [4800],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/customize_chrome/resources.grd": {
    "META": {"sizes": {"includes": [80],}},
    "includes": [4840],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/history_clusters/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [4860],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/read_anything/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [4880],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/reading_list/resources.grd": {
    "META": {"sizes": {"includes": [15],}},
    "includes": [4900],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/side_panel/shared/resources.grd": {
    "META": {"sizes": {"includes": [15],}},
    "includes": [4920],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/signin/batch_upload/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [4940],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/signin/profile_picker/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [4960],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/signin/resources.grd": {
    "META": {"sizes": {"includes": [90],}},
    "includes": [4980],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/signin/signout_confirmation/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [4990],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/suggest_internals/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [5000],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/support_tool/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [5020],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/tab_search/resources.grd": {
    "META": {"sizes": {"includes": [90]}},
    "includes": [5040],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/tab_strip/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [5060],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/tts_engine/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [5070],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/usb_internals/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [5080],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/user_education_internals/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [5090],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/web_app_internals/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [5100],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/webapks/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [5120],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/webui_gallery/resources.grd": {
    "META": {"sizes": {"includes": [90]}},
    "includes": [5140],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/webui_js_error/resources.grd": {
   "META": {"sizes": {"includes": [10],}},
   "includes": [5160],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/whats_new/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [5200],
  },
  # END chrome/ WebUI resources section

  # START chrome/ miscellaneous section.
  "chrome/common/common_resources.grd": {
    # Big alignment at start of section.
    "META": {"align": 100},
    "includes": [5500],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/common/chromeos/extensions/chromeos_system_extensions_resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [5520],
  },
  "chrome/credential_provider/gaiacp/gaia_resources.grd": {
    "includes": [5540],
    "messages": [5560],
  },
  "chrome/renderer/resources/renderer_resources.grd": {
    "includes": [5580],
    "structures": [5600],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chrome/test/data/webui/resources.grd": {
    "META": {"sizes": {"includes": [2500],}},
    "includes": [5620],
  },
  # END chrome/ miscellaneous section.

  # START chromeos/ section.
  "chromeos/chromeos_strings.grd": {
    # Big alignment at start of section.
    "META": {"align": 100},
    "messages": [5700],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/ambient/resources/lottie_resources.grd": {
    "META": {"sizes": {"includes": [100],}},
    "includes": [5720],
  },
  "chromeos/ash/components/emoji/emoji.grd" : {
    "META": {"sizes": {"includes": [45],}},
    "includes" : [5740],
  },
  "<(SHARED_INTERMEDIATE_DIR)/chromeos/ash/components/kiosk/vision/webui/resources.grd" : {
    "META": {"sizes": {"includes": [15]}},
    "includes" : [5760],
  },
  "chromeos/ash/resources/ash_resources.grd": {
    "includes": [5780],
  },
  "chromeos/ash/resources/internal/ash_internal_scaled_resources.grd": {
    "structures": [5800],
  },
  "chromeos/ash/resources/internal/ash_internal_strings.grd": {
    "messages": [5820],
  },
  # Both boca_app_bundle_resources.grd and boca_app_bundle_mock_resources.grd
  # start with the same id because only one of them is built depending on if
  # actual app is available.
  "ash/webui/boca_ui/resources/prod/boca_app_bundle_resources.grd": {
    "META": {"sizes": {"includes": [120],}},
    "includes": [5840],
  },
  "ash/webui/boca_ui/resources/mock/boca_app_bundle_mock_resources.grd": {
    "META": {"sizes": {"includes": [120],}},
    "includes": [5840],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/boca_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [5860],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/camera_app_ui/ash_camera_app_resources.grd": {
    "META": {"sizes": {"includes": [300],}},
    "includes": [5880],
  },
  "ash/webui/camera_app_ui/resources/strings/camera_strings.grd": {
    "messages": [5900],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/color_internals/resources/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [5920],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/common/resources/office_fallback/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [5940],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/connectivity_diagnostics/resources/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [5960],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/diagnostics_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [200],}},
    "includes": [5980],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/file_manager/resources/file_manager_swa_resources.grd": {
    "META": {"sizes": {"includes": [200]}},
    "includes": [6000],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/file_manager/untrusted_resources/file_manager_untrusted_resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [6020],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/files_internals/ash_files_internals_resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [6040],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/focus_mode/resources/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [6060],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/focus_mode/untrusted_resources/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [6080],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/graduation/resources/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [6090],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/recorder_app_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [200],}},
    "includes": [6100],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/common/resources/resources.grd": {
    "META": {"sizes": {"includes": [1000]}},
    "includes": [6120],
  },
  "ash/webui/help_app_ui/resources/help_app_resources.grd": {
    "includes": [6140],
  },
  # Both help_app_kids_magazine_bundle_resources.grd and
  # help_app_kids_magazine_bundle_mock_resources.grd start with the same id
  # because only one of them is built depending on if src_internal is available.
  # Lower bound for number of resource ids is the number of files, which is 3 in
  # in this case (HTML, JS and CSS file).
  "ash/webui/help_app_ui/resources/prod/help_app_kids_magazine_bundle_resources.grd": {
    "META": {"sizes": {"includes": [15],}},
    "includes": [6160],
  },
  "ash/webui/help_app_ui/resources/mock/help_app_kids_magazine_bundle_mock_resources.grd": {
    "includes": [6160],
  },
  # Both help_app_bundle_resources.grd and help_app_bundle_mock_resources.grd
  # start with the same id because only one of them is built depending on if
  # src_internal is available. Lower bound is that we bundle ~100 images for
  # offline articles with the app, as well as strings in every language (74),
  # and bundled content in the top 25 languages (25 x 2).
  "ash/webui/help_app_ui/resources/prod/help_app_bundle_resources.grd": {
    "META": {"sizes": {"includes": [300],}},  # Relies on src-internal.
    "includes": [6180],
  },
  "ash/webui/help_app_ui/resources/mock/help_app_bundle_mock_resources.grd": {
    "includes": [6180],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/mall/resources/resources.grd": {
    "META": {"join": 2, "sizes": {"includes": [10],}},
    "includes": [6200],
  },
  "ash/webui/media_app_ui/resources/media_app_resources.grd": {
    "includes": [6220],
  },
  # Both media_app_bundle_resources.grd and media_app_bundle_mock_resources.grd
  # start with the same id because only one of them is built depending on if
  # src_internal is available. Lower bound for number of resource ids is number
  # of languages (74).
  "ash/webui/media_app_ui/resources/prod/media_app_bundle_resources.grd": {
    "META": {"sizes": {"includes": [130],}},  # Relies on src-internal.
    "includes": [6240],
  },
  "ash/webui/media_app_ui/resources/mock/media_app_bundle_mock_resources.grd": {
    "includes": [6240],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/print_management/resources/resources.grd": {
    "META": {"join": 2, "sizes": {"includes": [20]}},
    "includes": [6260],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/print_preview_cros/resources/resources.grd": {
    "META": {"sizes": {"includes": [50]}},
    "includes": [6280],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/sample_system_web_app_ui/resources/trusted/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6300],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/sample_system_web_app_ui/resources/untrusted/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6320],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/sanitize_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6340],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/scanning/resources/resources.grd": {
    "META": {"sizes": {"includes": [100],}},
    "includes": [6360],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/status_area_internals/resources/resources.grd": {
    "META": {"sizes": {"includes": [30],}},
    "includes": [6380],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/eche_app_ui/ash_eche_app_resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6420],
  },
  # Both ash_eche_bundle_resources.grd and ash_eche_bundle_mock_resources.grd
  # start with the same id because only one of them is built depending on if
  # src_internal is available.
  "ash/webui/eche_app_ui/resources/prod/ash_eche_bundle_resources.grd": {
    "META": {"sizes": {"includes": [120],}},
    "includes": [6440],
  },
  "ash/webui/eche_app_ui/resources/mock/ash_eche_bundle_mock_resources.grd": {
    "META": {"sizes": {"includes": [120],}},
    "includes": [6440],
  },
  "ash/webui/multidevice_debug/resources/multidevice_debug_resources.grd": {
    "META": {"join": 2},
    "includes": [6460],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/scanner_feedback_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [6470],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/personalization_app/resources/resources.grd": {
    "META": {"sizes": {"includes": [200],}},
    "includes": [6480],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/demo_mode_app_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
   "includes": [6500],
  },

 "<(SHARED_INTERMEDIATE_DIR)/ash/webui/projector_app/resources/app/untrusted/ash_projector_app_untrusted_resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6520],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/annotator/resources/untrusted/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6540],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/projector_app/resources/common/ash_projector_common_resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [6560],
  },

  # Both projector_app_bundle_resources.grd and projector_app_bundle_mock_resources.grd
  # start with the same id because only one of them is built depending on if
  # src_internal is available. Lower bound for number of resource ids is number
  # of languages (79).
  "ash/webui/projector_app/resources/prod/projector_app_bundle_resources.grd": {
    "META": {"sizes": {"includes": [120],}}, # Relies on src-internal.
    "includes": [6580],
  },
  "ash/webui/projector_app/resources/mock/projector_app_bundle_mock_resources.grd": {
    "includes": [6580],
  },

  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/vc_background_ui/resources/resources.grd": {
    "META": {"join": 2, "sizes": {"includes": [50],}},
    "includes": [6600],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/growth_internals/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [6620],
  },
  # END chromeos/ section.

  # START components/ section.
  "components/autofill/core/browser/geo/autofill_address_rewriter_resources.grd":{
    "META": {"align": 1000},
    "includes": [7000]
  },
  # Chromium strings and Google Chrome strings must start at the same id.
  # We only use one file depending on whether we're building Chromium or
  # Google Chrome.
  "components/components_chromium_strings.grd": {
    # Big alignment at start of section.
    "messages": [7020],
  },
  "components/components_google_chrome_strings.grd": {
    "messages": [7020],
  },
  "components/components_locale_settings.grd": {
    "META": {"join": 2},
    "includes": [7040],
    "messages": [7060],
  },
  "components/components_strings.grd": {
    "messages": [7080],
  },
  "components/embedder_support/android/java/strings/web_contents_delegate_android_strings.grd": {
    "messages": [7100],
  },
  "components/headless/command_handler/headless_command.grd": {
    "includes": [7120],
  },
  # metrics/internal/server_urls.grd and metrics/server_urls.grd must share the
  # same id because they define the same strings, but only one of them is built
  # depending on whether src_internal is available.
  "components/metrics/internal/server_urls.grd": {
    "messages": [7130],
  },
  "components/metrics/server_urls.grd": {
    "messages": [7130],
  },
  "components/omnibox/resources/omnibox_pedal_synonyms.grd": {
    "META": {"join": 2},
    "messages": [7140],
  },
  # plus_addresses_internal_strings.grd and plus_addresses_strings.grd must
  # share the same id because they define the same strings, but only one of them
  # is built depending on whether src_internal is available.
  "components/plus_addresses/resources/internal/strings/plus_addresses_internal_strings.grd": {
    "messages": [7160],
  },
  "components/plus_addresses/resources/strings/plus_addresses_strings.grd": {
    "messages": [7160],
  },
  # components/policy/resources/policy_templates.grd and
  # components/policy/resources/policy_templates.build.grd must share the same
  # id because they are based on the same structure, however they are used in
  # different pipelines.
  "components/policy/resources/policy_templates.grd": {
    "structures": [7180],
  },
  "components/policy/resources/policy_templates.build.grd": {
    "structures": [7180],
  },
  "components/privacy_sandbox_strings.grd": {
    "messages": [7200],
  },
  "components/resources/components_resources.grd": {
    "includes": [7220],
  },
  "components/resources/components_scaled_resources.grd": {
    "structures": [7240],
  },
  "components/resources/dev_ui_components_resources.grd": {
    "includes": [7260],
  },
  "components/search_engine_descriptions_strings.grd": {
    "messages": [7280],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/autofill/core/browser/autofill_and_password_manager_internals/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7290],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/commerce/core/internals/resources/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [7300],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/download/resources/download_internals/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [7320],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/crash/core/browser/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7330],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/gcm_driver/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7350],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/history_clusters/history_clusters_internals/resources/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [7360],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/management/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7370],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/metrics/debug/resources.grd": {
    "META": {"sizes": {"includes": [15]}},
    "includes": [7380],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/net_log/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7385],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/ntp_tiles/webui/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7390],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/optimization_guide/optimization_guide_internals/resources/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [7400],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/policy/resources/webui/resources.grd": {
    "META": {"sizes": {"includes": [30]}},
    "includes": [7420],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/safe_browsing/content/browser/web_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7425],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/signin/core/browser/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7430],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/translate/translate_internals/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7435],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/ukm/debug/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7440],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/webui/chrome_urls/resources/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [7450],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/webui/flags/resources/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [7455],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/webui/internal_debug_pages_disabled/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7460],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/webui/user_actions/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7465],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/webui/version/resources/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [7470],
  },
  "<(SHARED_INTERMEDIATE_DIR)/components/sync/service/resources/resources.grd": {
   "META": {"sizes": {"includes": [30],}},
    "includes": [7480],
  },
  # END components/ section.

  # START ios/ section.
  #
  # chrome/ and ios/chrome/ must start at the same id.
  # App only use one file depending on whether it is iOS or other platform.
  "ios/chrome/app/resources/ios_resources.grd": {
    "includes": [800],
    "structures": [820],
  },

  "<(SHARED_INTERMEDIATE_DIR)/ios/chrome/app/resources/profile_internals/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [850],
  },

  # Chromium strings and Google Chrome strings must start at the same id.
  # We only use one file depending on whether we're building Chromium or
  # Google Chrome.
  "ios/chrome/app/strings/ios_chromium_strings.grd": {
    # Big alignment to make start IDs look nicer.
    "META": {"align": 100},
    "messages": [900],
  },
  "ios/chrome/app/strings/ios_google_chrome_strings.grd": {
    "messages": [900],
  },

  "ios/chrome/app/strings/ios_strings.grd": {
    # Big alignment since strings (previous item) are frequently added.
    "META": {"join": 2, "align": 200},
    "messages": [1000],
  },
  "ios/chrome/app/theme/ios_theme_resources.grd": {
    # Big alignment since strings (previous item) are frequently added.
    "META": {"align": 100},
    "structures": [1100],
  },
  "ios/chrome/browser/ui/whats_new/strings/ios_whats_new_strings.grd": {
    "messages": [1120],
  },
  "ios/chrome/share_extension/strings/ios_share_extension_strings.grd": {
    "messages": [1140],
  },
  "ios/chrome/open_extension/strings/ios_open_extension_chromium_strings.grd": {
    "messages": [1160],
  },
  "ios/chrome/open_extension/strings/ios_open_extension_google_chrome_strings.grd": {
    "messages": [1160],
  },
  "ios/chrome/search_widget_extension/strings/ios_search_widget_extension_chromium_strings.grd": {
    "META": {"join": 2},
    "messages": [1180],
  },
  "ios/chrome/search_widget_extension/strings/ios_search_widget_extension_google_chrome_strings.grd": {
    "messages": [1180],
  },
  "ios/chrome/content_widget_extension/strings/ios_content_widget_extension_chromium_strings.grd": {
    "META": {"join": 2},
    "messages": [1200],
  },
  "ios/chrome/content_widget_extension/strings/ios_content_widget_extension_google_chrome_strings.grd": {
    "messages": [1200],
  },
  "ios/chrome/credential_provider_extension/strings/ios_credential_provider_extension_strings.grd": {
    "META": {"join": 2},
    "messages": [1220],
  },
  "ios/chrome/widget_kit_extension/strings/ios_widget_kit_extension_strings.grd": {
    "messages": [1320],
  },
  "ios/web/ios_web_resources.grd": {
    "includes": [1340],
  },
  "ios/web/test/test_resources.grd": {
    "includes": [1360],
  },
  # END ios/ section.

  # START ios_internal/ section.
  "ios_internal/chrome/app/ios_internal_strings.grd": {
    "messages": [1380],
  },
  "ios_internal/chrome/app/ios_internal_chromium_strings.grd": {
    "META": {"join": 2},
    "messages": [7600],
  },
  "ios_internal/chrome/app/ios_internal_google_chrome_strings.grd": {
    "messages": [7620],
  },
  # END ios_internal/ section.

  # START content/ section.
  "content/content_resources.grd": {
    # Big alignment at start of section.
    "META": {"join": 2, "align": 100},
    "includes": [8000],
  },
  "content/shell/shell_resources.grd": {
    "includes": [8020],
  },
  "content/test/web_ui_mojo_test_resources.grd": {
    "includes": [8040],
  },

  # These files are generated during the build.
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/private_aggregation/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [8060],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/gpu/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [8080],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/histograms/resources.grd": {
    "META": {"sizes": {"includes": [5]}},
    "includes": [8100],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/indexed_db/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [8120],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/media/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [8140],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/net/resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [8160],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/process/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [8180],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/service_worker/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [8200],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/quota/resources.grd": {
    "META": {"sizes": {"includes": [10],}},
    "includes": [8220],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/traces_internals/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [8240],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/webxr_internals/resources.grd": {
    "META": {"sizes": {"includes": [20,],}},
    "includes": [8260],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/resources/attribution_reporting/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [8280],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/tracing/tracing_resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [8300],
  },
  "<(SHARED_INTERMEDIATE_DIR)/content/browser/webrtc/resources/resources.grd": {
    "META": {"sizes": {"includes": [20],}},
    "includes": [8320],
  },
  # END content/ section.

  # START "everything else" section.
  # Everything but chrome/, chromeos/, components/, content/, and ios/
  "ash/ash_strings.grd": {
    # Big alignment at start of section.
    "META": {"align": 100},
    "messages": [9000],
  },
  # TODO(b/207518736): Input overlay resources will be changed to proto soon,
  # thus not rushing to update it for now.
  "chromeos/ash/experiences/arc/input_overlay/resources/input_overlay_resources.grd": {
    # Big alignment at start of section.
    "includes": [9010],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/os_feedback_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [9020],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/os_feedback_ui/untrusted_resources/resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [9040],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/firmware_update_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [200],}},
    "includes": [9060],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/shortcut_customization_ui/resources/resources.grd": {
    "META": {"sizes": {"includes": [200],}},
    "includes": [9080],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ash/webui/shimless_rma/resources/resources.grd": {
    "META": {"sizes": {"includes": [100],}},
    "includes": [9100],
  },
  "ash/keyboard/ui/keyboard_resources.grd": {
    "includes": [9120],
  },
  "ash/login/resources/login_resources.grd": {
    "structures": [9140],
  },
  "ash/public/cpp/resources/ash_public_unscaled_resources.grd": {
    "includes": [9160],
    "structures": [9180],
  },
  "ash/quick_insert/resources/quick_insert_resources.grd": {
    "structures":[9190],
  },
  "ash/system/mahi/resources/mahi_resources.grd": {
    "structures":[9200],
  },
  "ash/system/video_conference/resources/vc_resources.grd": {
    "structures":[9220],
  },
  "ash/wm/overview/birch/resources/coral_resources.grd": {
    "structures":[9230],
  },
  "base/tracing/protos/resources.grd": {
    "includes": [9240],
  },
  "chromecast/app/resources/chromecast_settings.grd": {
    "messages": [9260],
  },
  "chromecast/app/resources/shell_resources.grd": {
    "includes": [9280],
  },
  "chromecast/renderer/resources/extensions_renderer_resources.grd": {
    "includes": [9300],
  },

  "device/bluetooth/bluetooth_strings.grd": {
    "messages": [9320],
  },

  "device/fido/fido_strings.grd": {
    "messages": [9340],
  },

  "extensions/browser/resources/extensions_browser_resources.grd": {
    "structures": [9360],
  },
  "extensions/extensions_resources.grd": {
    "includes": [9380],
  },
  "extensions/renderer/resources/extensions_renderer_resources.grd": {
    "includes": [9400],
    "structures": [9420],
  },
  "extensions/shell/app_shell_resources.grd": {
    "includes": [9440],
  },
  "extensions/strings/extensions_strings.grd": {
    "messages": [9460],
  },

  "mojo/public/js/mojo_bindings_resources.grd": {
    "includes": [9480],
  },

  "net/base/net_resources.grd": {
    "includes": [9500],
  },

  "remoting/resources/remoting_strings.grd": {
    "messages": [9520],
  },

  "services/services_strings.grd": {
    "messages": [9540],
  },
  "third_party/blink/public/blink_image_resources.grd": {
    "structures": [9560],
  },
  "third_party/blink/public/blink_resources.grd": {
    "includes": [9580],
  },
  "third_party/blink/public/strings/blink_strings.grd": {
    "messages": [9600],
  },
  "third_party/blink/public/strings/permission_element_strings.grd": {
    "messages": [9620],
  },
  "third_party/blink/renderer/modules/media_controls/resources/media_controls_resources.grd": {
    "includes": [9640],
    "structures": [9640],
  },
  "third_party/libaddressinput/chromium/address_input_strings.grd": {
    "messages": [9680],
  },

  "ui/base/test/ui_base_test_resources.grd": {
    "messages": [9700],
  },
  "ui/chromeos/resources/ui_chromeos_resources.grd": {
    "structures": [9720],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ui/chromeos/styles/cros_typography_resources.grd": {
    "META": {"sizes": {"includes": [5],}},
    "includes": [9740],
  },
  "ui/chromeos/ui_chromeos_strings.grd": {
    "messages": [9760],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ui/file_manager/file_manager_gen_resources.grd": {
    "META": {"sizes": {"includes": [2000]}},
    "includes": [9780],
  },
  "ui/file_manager/file_manager_resources.grd": {
    "includes": [9800],
  },
  "ui/resources/ui_resources.grd": {
    "structures": [9820],
  },
  "ui/resources/ui_unscaled_resources.grd": {
    "includes": [9840],
  },
  "ui/strings/app_locale_settings.grd": {
    "messages": [9860],
  },
  "ui/strings/auto_image_annotation_strings.grd": {
    "messages": [9870],
  },
  "ui/strings/ax_strings.grd": {
    "messages": [9880],
  },
  "ui/strings/ui_strings.grd": {
    "messages": [9900],
  },
  "ui/views/examples/views_examples_resources.grd": {
    "messages": [9920],
  },
  "ui/views/resources/views_resources.grd": {
    "structures": [9940],
  },
  "ui/webui/examples/resources/webui_examples_resources.grd": {
    "messages": [9960],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ui/webui/examples/resources/browser/resources.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [9980],
  },
  "<(SHARED_INTERMEDIATE_DIR)/ui/webui/resources/webui_resources.grd": {
    "META": {"sizes": {"includes": [1100]}},
    "includes": [10000],
  },
  "weblayer/weblayer_resources.grd": {
    "includes": [10020],
  },

  # This file is generated during the build.
  # .grd extension is required because it's checked before var interpolation.
  "<(DEVTOOLS_GRD_PATH).grd": {
    # In debug build, devtools frontend sources are not bundled and therefore
    # includes a lot of individual resources
    "META": {"sizes": {"includes": [4000],}},
    "includes": [10040],
  },

  # This file is generated during the build.
  "<(SHARED_INTERMEDIATE_DIR)/resources/inspector_overlay/inspector_overlay_resources.grd": {
    "META": {"sizes": {"includes": [50],}},
    "includes": [10060],
  },

  "<(SHARED_INTERMEDIATE_DIR)/third_party/blink/public/strings/permission_element_generated_strings.grd": {
    "META": {"sizes": {"messages": [2000],}},
    "messages": [10080],
  }

  # END "everything else" section.
  # Everything but chrome/, components/, content/, and ios/

  # Thinking about appending to the end?
  # Please read the header and find the right section above instead.
}
