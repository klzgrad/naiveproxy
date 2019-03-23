#!/usr/bin/env python
# coding: utf-8
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import os
import unittest
import sys

import build_utils  # pylint: disable=relative-import

# Required because the following import needs build/android/gyp in the
# Python path to import util.build_utils.
_BUILD_ANDROID_GYP_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(1, _BUILD_ANDROID_GYP_ROOT)

import resource_utils  # pylint: disable=relative-import

_RES_STRINGS_1 = {
    'low_memory_error': 'Eelmist toimingut ei saa vähese mälu tõttu lõpetada',
    'opening_file_error': 'Valit. faili avamine ebaõnnestus',
    'copy_to_clipboard_failure_message': 'Lõikelauale kopeerimine ebaõnnestus'
}

# pylint: disable=line-too-long
_EXPECTED_XML_1 = '''<?xml version="1.0" encoding="utf-8"?>
<resources xmlns:android="http://schemas.android.com/apk/res/android">
<string name="copy_to_clipboard_failure_message">"Lõikelauale kopeerimine ebaõnnestus"</string>
<string name="low_memory_error">"Eelmist toimingut ei saa vähese mälu tõttu lõpetada"</string>
<string name="opening_file_error">"Valit. faili avamine ebaõnnestus"</string>
</resources>
'''
# pylint: enable=line-too-long

_XML_RESOURCES_PREFIX = r'''<?xml version="1.0" encoding="utf-8"?>
<resources xmlns:android="http://schemas.android.com/apk/res/android">
'''

_XML_RESOURCES_SUFFIX = '</resources>\n'

# Extracted from one generated Chromium R.txt file, with string resource
# names shuffled randomly.
_TEST_R_TXT = r'''int anim abc_fade_in 0x7f050000
int anim abc_fade_out 0x7f050001
int anim abc_grow_fade_in_from_bottom 0x7f050002
int array DefaultCookiesSettingEntries 0x7f120002
int array DefaultCookiesSettingValues 0x7f120003
int array DefaultGeolocationSettingEntries 0x7f120004
int attr actionBarDivider 0x7f0100e7
int attr actionBarStyle 0x7f0100e2
int string AllowedDomainsForAppsDesc 0x7f0c0105
int string AlternateErrorPagesEnabledDesc 0x7f0c0107
int string AuthAndroidNegotiateAccountTypeDesc 0x7f0c0109
int string AllowedDomainsForAppsTitle 0x7f0c0104
int string AlternateErrorPagesEnabledTitle 0x7f0c0106
int[] styleable SnackbarLayout { 0x0101011f, 0x7f010076, 0x7f0100ba }
int styleable SnackbarLayout_android_maxWidth 0
int styleable SnackbarLayout_elevation 2
'''

# Test whitelist R.txt file. Note that AlternateErrorPagesEnabledTitle is
# listed as an 'anim' and should thus be skipped. Similarly the string
# 'ThisStringDoesNotAppear' should not be in the final result.
_TEST_WHITELIST_R_TXT = r'''int anim AlternateErrorPagesEnabledTitle 0x7f0eeeee
int string AllowedDomainsForAppsDesc 0x7f0c0105
int string AlternateErrorPagesEnabledDesc 0x7f0c0107
int string ThisStringDoesNotAppear 0x7f0fffff
'''

_TEST_R_TEXT_RESOURCES_IDS = {
    0x7f0c0105: 'AllowedDomainsForAppsDesc',
    0x7f0c0107: 'AlternateErrorPagesEnabledDesc',
}

# Names of string resources in _TEST_R_TXT, should be sorted!
_TEST_R_TXT_STRING_RESOURCE_NAMES = sorted([
    'AllowedDomainsForAppsDesc',
    'AllowedDomainsForAppsTitle',
    'AlternateErrorPagesEnabledDesc',
    'AlternateErrorPagesEnabledTitle',
    'AuthAndroidNegotiateAccountTypeDesc',
])


def _CreateTestFile(tmp_dir, file_name, file_data):
  file_path = os.path.join(tmp_dir, file_name)
  with open(file_path, 'wt') as f:
    f.write(file_data)
  return file_path



class ResourceUtilsTest(unittest.TestCase):

  def test_GetRTxtStringResourceNames(self):
    with build_utils.TempDir() as tmp_dir:
      tmp_file = _CreateTestFile(tmp_dir, "test_R.txt", _TEST_R_TXT)
      self.assertListEqual(
          resource_utils.GetRTxtStringResourceNames(tmp_file),
          _TEST_R_TXT_STRING_RESOURCE_NAMES)

  def test_GenerateStringResourcesWhitelist(self):
    with build_utils.TempDir() as tmp_dir:
      tmp_module_rtxt_file = _CreateTestFile(tmp_dir, "test_R.txt", _TEST_R_TXT)
      tmp_whitelist_rtxt_file = _CreateTestFile(tmp_dir, "test_whitelist_R.txt",
                                                _TEST_WHITELIST_R_TXT)
      self.assertDictEqual(
          resource_utils.GenerateStringResourcesWhitelist(
              tmp_module_rtxt_file, tmp_whitelist_rtxt_file),
          _TEST_R_TEXT_RESOURCES_IDS)

  def test_IsAndroidLocaleQualifier(self):
    good_locales = [
        'en',
        'en-rUS',
        'fil',
        'fil-rPH',
        'iw',
        'iw-rIL',
        'b+en',
        'b+en+US',
        'b+ja+Latn',
        'b+ja+JP+Latn',
        'b+cmn+Hant-TW',
    ]
    bad_locales = [
        'e', 'english', 'en-US', 'en_US', 'en-rus', 'b+e', 'b+english', 'b+ja+'
    ]
    for locale in good_locales:
      self.assertTrue(
          resource_utils.IsAndroidLocaleQualifier(locale),
          msg="'%s' should be a good locale!" % locale)

    for locale in bad_locales:
      self.assertFalse(
          resource_utils.IsAndroidLocaleQualifier(locale),
          msg="'%s' should be a bad locale!" % locale)

  def test_ToAndroidLocaleName(self):
    _TEST_CHROMIUM_TO_ANDROID_LOCALE_MAP = {
        'en': 'en',
        'en-US': 'en-rUS',
        'en-FOO': 'en-rFOO',
        'fil': 'tl',
        'tl': 'tl',
        'he': 'iw',
        'he-IL': 'iw-rIL',
        'id': 'in',
        'id-BAR': 'in-rBAR',
        'nb': 'nb',
        'yi': 'ji'
    }
    for chromium_locale, android_locale in \
        _TEST_CHROMIUM_TO_ANDROID_LOCALE_MAP.iteritems():
      result = resource_utils.ToAndroidLocaleName(chromium_locale)
      self.assertEqual(result, android_locale)

  def test_ToChromiumLocaleName(self):
    _TEST_ANDROID_TO_CHROMIUM_LOCALE_MAP = {
        'foo': 'foo',
        'foo-rBAR': 'foo-BAR',
        'b+foo': 'foo',
        'b+foo+BAR': 'foo-BAR',
        'b+foo+BAR+Whatever': 'foo-BAR',
        'b+foo+Whatever+BAR': 'foo-BAR',
        'b+foo+Whatever': 'foo',
        'en': 'en',
        'en-rUS': 'en-US',
        'en-US': None,
        'en-FOO': None,
        'en-rFOO': 'en-FOO',
        'es-rES': 'es-ES',
        'es-rUS': 'es-419',
        'tl': 'fil',
        'fil': 'fil',
        'iw': 'he',
        'iw-rIL': 'he-IL',
        'in': 'id',
        'in-rBAR': 'id-BAR',
        'id-rBAR': 'id-BAR',
        'nb': 'nb',
        'no': 'nb',  # http://crbug.com/920960
    }
    for android_locale, chromium_locale in \
        _TEST_ANDROID_TO_CHROMIUM_LOCALE_MAP.iteritems():
      result = resource_utils.ToChromiumLocaleName(android_locale)
      self.assertEqual(result, chromium_locale)

  def test_FindLocaleInStringResourceFilePath(self):
    self.assertEqual(
        None,
        resource_utils.FindLocaleInStringResourceFilePath(
            'res/values/whatever.xml'))
    self.assertEqual(
        'foo',
        resource_utils.FindLocaleInStringResourceFilePath(
            'res/values-foo/whatever.xml'))
    self.assertEqual(
        'foo-rBAR',
        resource_utils.FindLocaleInStringResourceFilePath(
            'res/values-foo-rBAR/whatever.xml'))
    self.assertEqual(
        None,
        resource_utils.FindLocaleInStringResourceFilePath(
            'res/values-foo/ignore-subdirs/whatever.xml'))


if __name__ == '__main__':
  unittest.main()
