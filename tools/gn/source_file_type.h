// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SOURCE_FILE_TYPE_H_
#define TOOLS_GN_SOURCE_FILE_TYPE_H_

class SourceFile;

// This should be sequential integers starting from 0 so they can be used as
// array indices.
enum SourceFileType {
  SOURCE_UNKNOWN = 0,
  SOURCE_ASM,
  SOURCE_C,
  SOURCE_CPP,
  SOURCE_H,
  SOURCE_M,
  SOURCE_MM,
  SOURCE_S,
  SOURCE_RC,
  SOURCE_O,  // Object files can be inputs, too. Also counts .obj.
  SOURCE_DEF,

  // Must be last.
  SOURCE_NUMTYPES,
};

SourceFileType GetSourceFileType(const SourceFile& file);

#endif  // TOOLS_GN_SOURCE_FILE_TYPE_H_
