#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Constant definitions for GRIT.
'''


# This is the Icelandic noun meaning "grit" and is used to check that our
# input files are in the correct encoding.  The middle character gets encoded
# as two bytes in UTF-8, so this is sufficient to detect incorrect encoding.
ENCODING_CHECK = u'm\u00f6l'

# A special language, translations into which are always "TTTTTT".
CONSTANT_LANGUAGE = 'x_constant'

FAKE_BIDI = 'fake-bidi'
