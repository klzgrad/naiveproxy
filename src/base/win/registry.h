// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_REGISTRY_H_
#define BASE_WIN_REGISTRY_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/win/object_watcher.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_types.h"

namespace base {
namespace win {

// Utility class to read, write and manipulate the Windows Registry.
// Registry vocabulary primer: a "key" is like a folder, in which there
// are "values", which are <name, data> pairs, with an associated data type.
//
// Note:
//  * ReadValue family of functions guarantee that the out-parameter
//    is not touched in case of failure.
//  * Functions returning LONG indicate success as ERROR_SUCCESS or an
//    error as a (non-zero) win32 error code.
class BASE_EXPORT RegKey {
 public:
  // Called from the MessageLoop when the key changes.
  using ChangeCallback = OnceCallback<void()>;

  RegKey();
  explicit RegKey(HKEY key);
  RegKey(HKEY rootkey, const char16* subkey, REGSAM access);
  ~RegKey();

  LONG Create(HKEY rootkey, const char16* subkey, REGSAM access);

  LONG CreateWithDisposition(HKEY rootkey,
                             const char16* subkey,
                             DWORD* disposition,
                             REGSAM access);

  // Creates a subkey or open it if it already exists.
  LONG CreateKey(const char16* name, REGSAM access);

  // Opens an existing reg key.
  LONG Open(HKEY rootkey, const char16* subkey, REGSAM access);

  // Opens an existing reg key, given the relative key name.
  LONG OpenKey(const char16* relative_key_name, REGSAM access);

  // Closes this reg key.
  void Close();

  // Replaces the handle of the registry key and takes ownership of the handle.
  void Set(HKEY key);

  // Transfers ownership away from this object.
  HKEY Take();

  // Returns false if this key does not have the specified value, or if an error
  // occurrs while attempting to access it.
  bool HasValue(const char16* value_name) const;

  // Returns the number of values for this key, or 0 if the number cannot be
  // determined.
  DWORD GetValueCount() const;

  // Determines the nth value's name.
  LONG GetValueNameAt(int index, string16* name) const;

  // True while the key is valid.
  bool Valid() const { return key_ != NULL; }

  // Kills a key and everything that lives below it; please be careful when
  // using it.
  LONG DeleteKey(const char16* name);

  // Deletes an empty subkey.  If the subkey has subkeys or values then this
  // will fail.
  LONG DeleteEmptyKey(const char16* name);

  // Deletes a single value within the key.
  LONG DeleteValue(const char16* name);

  // Getters:

  // Reads a REG_DWORD (uint32_t) into |out_value|. If |name| is null or empty,
  // reads the key's default value, if any.
  LONG ReadValueDW(const char16* name, DWORD* out_value) const;

  // Reads a REG_QWORD (int64_t) into |out_value|. If |name| is null or empty,
  // reads the key's default value, if any.
  LONG ReadInt64(const char16* name, int64_t* out_value) const;

  // Reads a string into |out_value|. If |name| is null or empty, reads
  // the key's default value, if any.
  LONG ReadValue(const char16* name, string16* out_value) const;

  // Reads a REG_MULTI_SZ registry field into a vector of strings. Clears
  // |values| initially and adds further strings to the list. Returns
  // ERROR_CANTREAD if type is not REG_MULTI_SZ.
  LONG ReadValues(const char16* name, std::vector<string16>* values);

  // Reads raw data into |data|. If |name| is null or empty, reads the key's
  // default value, if any.
  LONG ReadValue(const char16* name,
                 void* data,
                 DWORD* dsize,
                 DWORD* dtype) const;

  // Setters:

  // Sets an int32_t value.
  LONG WriteValue(const char16* name, DWORD in_value);

  // Sets a string value.
  LONG WriteValue(const char16* name, const char16* in_value);

  // Sets raw data, including type.
  LONG WriteValue(const char16* name,
                  const void* data,
                  DWORD dsize,
                  DWORD dtype);

  // Starts watching the key to see if any of its values have changed.
  // The key must have been opened with the KEY_NOTIFY access privilege.
  // Returns true on success.
  // To stop watching, delete this RegKey object. To continue watching the
  // object after the callback is invoked, call StartWatching again.
  bool StartWatching(ChangeCallback callback);

  HKEY Handle() const { return key_; }

 private:
  class Watcher;

  // Calls RegDeleteKeyEx on supported platforms, alternatively falls back to
  // RegDeleteKey.
  static LONG RegDeleteKeyExWrapper(HKEY hKey,
                                    const char16* lpSubKey,
                                    REGSAM samDesired,
                                    DWORD Reserved);

  // Recursively deletes a key and all of its subkeys.
  static LONG RegDelRecurse(HKEY root_key, const char16* name, REGSAM access);

  HKEY key_;  // The registry key being iterated.
  REGSAM wow64access_;
  std::unique_ptr<Watcher> key_watcher_;

  DISALLOW_COPY_AND_ASSIGN(RegKey);
};

// Iterates the entries found in a particular folder on the registry.
class BASE_EXPORT RegistryValueIterator {
 public:
  // Constructs a Registry Value Iterator with default WOW64 access.
  RegistryValueIterator(HKEY root_key, const char16* folder_key);

  // Constructs a Registry Key Iterator with specific WOW64 access, one of
  // KEY_WOW64_32KEY or KEY_WOW64_64KEY, or 0.
  // Note: |wow64access| should be the same access used to open |root_key|
  // previously, or a predefined key (e.g. HKEY_LOCAL_MACHINE).
  // See http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
  RegistryValueIterator(HKEY root_key,
                        const char16* folder_key,
                        REGSAM wow64access);

  ~RegistryValueIterator();

  DWORD ValueCount() const;

  // True while the iterator is valid.
  bool Valid() const;

  // Advances to the next registry entry.
  void operator++();

  const char16* Name() const { return name_.c_str(); }
  const char16* Value() const { return value_.data(); }
  // ValueSize() is in bytes.
  DWORD ValueSize() const { return value_size_; }
  DWORD Type() const { return type_; }

  int Index() const { return index_; }

 private:
  // Reads in the current values.
  bool Read();

  void Initialize(HKEY root_key, const char16* folder_key, REGSAM wow64access);

  // The registry key being iterated.
  HKEY key_;

  // Current index of the iteration.
  int index_;

  // Current values.
  string16 name_;
  std::vector<char16> value_;
  DWORD value_size_;
  DWORD type_;

  DISALLOW_COPY_AND_ASSIGN(RegistryValueIterator);
};

class BASE_EXPORT RegistryKeyIterator {
 public:
  // Constructs a Registry Key Iterator with default WOW64 access.
  RegistryKeyIterator(HKEY root_key, const char16* folder_key);

  // Constructs a Registry Value Iterator with specific WOW64 access, one of
  // KEY_WOW64_32KEY or KEY_WOW64_64KEY, or 0.
  // Note: |wow64access| should be the same access used to open |root_key|
  // previously, or a predefined key (e.g. HKEY_LOCAL_MACHINE).
  // See http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
  RegistryKeyIterator(HKEY root_key,
                      const char16* folder_key,
                      REGSAM wow64access);

  ~RegistryKeyIterator();

  DWORD SubkeyCount() const;

  // True while the iterator is valid.
  bool Valid() const;

  // Advances to the next entry in the folder.
  void operator++();

  const char16* Name() const { return name_; }

  int Index() const { return index_; }

 private:
  // Reads in the current values.
  bool Read();

  void Initialize(HKEY root_key, const char16* folder_key, REGSAM wow64access);

  // The registry key being iterated.
  HKEY key_;

  // Current index of the iteration.
  int index_;

  char16 name_[MAX_PATH];

  DISALLOW_COPY_AND_ASSIGN(RegistryKeyIterator);
};

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_REGISTRY_H_
