// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file specifies a recursive data storage class called Value intended for
// storing settings and other persistable data.
//
// A Value represents something that can be stored in JSON or passed to/from
// JavaScript. As such, it is NOT a generalized variant type, since only the
// types supported by JavaScript/JSON are supported.
//
// IN PARTICULAR this means that there is no support for int64_t or unsigned
// numbers. Writing JSON with such types would violate the spec. If you need
// something like this, either use a double or make a string value containing
// the number you want.
//
// NOTE: A Value parameter that is always a Value::STRING should just be passed
// as a std::string. Similarly for Values that are always Value::DICTIONARY
// (should be flat_map), Value::LIST (should be std::vector), et cetera.

#ifndef BASE_VALUES_H_
#define BASE_VALUES_H_

#include <stddef.h>
#include <stdint.h>

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/value_iterators.h"

namespace base {

class DictionaryValue;
class ListValue;
class Value;

// The Value class is the base class for Values. A Value can be instantiated
// via passing the appropriate type or backing storage to the constructor.
//
// See the file-level comment above for more information.
//
// base::Value is currently in the process of being refactored. Design doc:
// https://docs.google.com/document/d/1uDLu5uTRlCWePxQUEHc8yNQdEoE1BDISYdpggWEABnw
//
// Previously (which is how most code that currently exists is written), Value
// used derived types to implement the individual data types, and base::Value
// was just a base class to refer to them. This required everything be heap
// allocated.
//
// OLD WAY:
//
//   std::unique_ptr<base::Value> GetFoo() {
//     std::unique_ptr<DictionaryValue> dict;
//     dict->SetString("mykey", foo);
//     return dict;
//   }
//
// The new design makes base::Value a variant type that holds everything in
// a union. It is now recommended to pass by value with std::move rather than
// use heap allocated values. The DictionaryValue and ListValue subclasses
// exist only as a compatibility shim that we're in the process of removing.
//
// NEW WAY:
//
//   base::Value GetFoo() {
//     base::Value dict(base::Value::Type::DICTIONARY);
//     dict.SetKey("mykey", base::Value(foo));
//     return dict;
//   }
class BASE_EXPORT Value {
 public:
  using BlobStorage = std::vector<char>;
  using DictStorage = base::flat_map<std::string, std::unique_ptr<Value>>;
  using ListStorage = std::vector<Value>;

  enum class Type {
    NONE = 0,
    BOOLEAN,
    INTEGER,
    DOUBLE,
    STRING,
    BINARY,
    DICTIONARY,
    LIST
    // Note: Do not add more types. See the file-level comment above for why.
  };

  // For situations where you want to keep ownership of your buffer, this
  // factory method creates a new BinaryValue by copying the contents of the
  // buffer that's passed in.
  // DEPRECATED, use std::make_unique<Value>(const BlobStorage&) instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  static std::unique_ptr<Value> CreateWithCopiedBuffer(const char* buffer,
                                                       size_t size);

  Value(Value&& that) noexcept;
  Value() noexcept;  // A null value.

  // Value's copy constructor and copy assignment operator are deleted. Use this
  // to obtain a deep copy explicitly.
  Value Clone() const;

  explicit Value(Type type);
  explicit Value(bool in_bool);
  explicit Value(int in_int);
  explicit Value(double in_double);

  // Value(const char*) and Value(const char16*) are required despite
  // Value(StringPiece) and Value(StringPiece16) because otherwise the
  // compiler will choose the Value(bool) constructor for these arguments.
  // Value(std::string&&) allow for efficient move construction.
  explicit Value(const char* in_string);
  explicit Value(StringPiece in_string);
  explicit Value(std::string&& in_string) noexcept;
  explicit Value(const char16* in_string16);
  explicit Value(StringPiece16 in_string16);

  explicit Value(const BlobStorage& in_blob);
  explicit Value(BlobStorage&& in_blob) noexcept;

  explicit Value(const DictStorage& in_dict);
  explicit Value(DictStorage&& in_dict) noexcept;

  explicit Value(const ListStorage& in_list);
  explicit Value(ListStorage&& in_list) noexcept;

  Value& operator=(Value&& that) noexcept;

  ~Value();

  // Returns the name for a given |type|.
  static const char* GetTypeName(Type type);

  // Returns the type of the value stored by the current Value object.
  Type GetType() const { return type_; }  // DEPRECATED, use type().
  Type type() const { return type_; }

  // Returns true if the current object represents a given type.
  bool IsType(Type type) const { return type == type_; }
  bool is_none() const { return type() == Type::NONE; }
  bool is_bool() const { return type() == Type::BOOLEAN; }
  bool is_int() const { return type() == Type::INTEGER; }
  bool is_double() const { return type() == Type::DOUBLE; }
  bool is_string() const { return type() == Type::STRING; }
  bool is_blob() const { return type() == Type::BINARY; }
  bool is_dict() const { return type() == Type::DICTIONARY; }
  bool is_list() const { return type() == Type::LIST; }

  // These will all fatally assert if the type doesn't match.
  bool GetBool() const;
  int GetInt() const;
  double GetDouble() const;  // Implicitly converts from int if necessary.
  const std::string& GetString() const;
  const BlobStorage& GetBlob() const;

  ListStorage& GetList();
  const ListStorage& GetList() const;

  // |FindKey| looks up |key| in the underlying dictionary. If found, it returns
  // a pointer to the element. Otherwise it returns nullptr.
  // returned. Callers are expected to perform a check against null before using
  // the pointer.
  // Note: This fatally asserts if type() is not Type::DICTIONARY.
  //
  // Example:
  //   auto* found = FindKey("foo");
  Value* FindKey(StringPiece key);
  const Value* FindKey(StringPiece key) const;

  // |FindKeyOfType| is similar to |FindKey|, but it also requires the found
  // value to have type |type|. If no type is found, or the found value is of a
  // different type nullptr is returned.
  // Callers are expected to perform a check against null before using the
  // pointer.
  // Note: This fatally asserts if type() is not Type::DICTIONARY.
  //
  // Example:
  //   auto* found = FindKey("foo", Type::DOUBLE);
  Value* FindKeyOfType(StringPiece key, Type type);
  const Value* FindKeyOfType(StringPiece key, Type type) const;

  // |SetKey| looks up |key| in the underlying dictionary and sets the mapped
  // value to |value|. If |key| could not be found, a new element is inserted.
  // A pointer to the modified item is returned.
  // Note: This fatally asserts if type() is not Type::DICTIONARY.
  //
  // Example:
  //   SetKey("foo", std::move(myvalue));
  Value* SetKey(StringPiece key, Value value);
  // This overload results in a performance improvement for std::string&&.
  Value* SetKey(std::string&& key, Value value);
  // This overload is necessary to avoid ambiguity for const char* arguments.
  Value* SetKey(const char* key, Value value);

  // This attemps to remove the value associated with |key|. In case of failure,
  // e.g. the key does not exist, |false| is returned and the underlying
  // dictionary is not changed. In case of success, |key| is deleted from the
  // dictionary and the method returns |true|.
  // Note: This fatally asserts if type() is not Type::DICTIONARY.
  //
  // Example:
  //   bool success = RemoveKey("foo");
  bool RemoveKey(StringPiece key);

  // Searches a hierarchy of dictionary values for a given value. If a path
  // of dictionaries exist, returns the item at that path. If any of the path
  // components do not exist or if any but the last path components are not
  // dictionaries, returns nullptr.
  //
  // The type of the leaf Value is not checked.
  //
  // Implementation note: This can't return an iterator because the iterator
  // will actually be into another Value, so it can't be compared to iterators
  // from this one (in particular, the DictItems().end() iterator).
  //
  // Example:
  //   auto* found = FindPath({"foo", "bar"});
  //
  //   std::vector<StringPiece> components = ...
  //   auto* found = FindPath(components);
  Value* FindPath(std::initializer_list<StringPiece> path);
  Value* FindPath(span<const StringPiece> path);
  const Value* FindPath(std::initializer_list<StringPiece> path) const;
  const Value* FindPath(span<const StringPiece> path) const;

  // Like FindPath but will only return the value if the leaf Value type
  // matches the given type. Will return nullptr otherwise.
  Value* FindPathOfType(std::initializer_list<StringPiece> path, Type type);
  Value* FindPathOfType(span<const StringPiece> path, Type type);
  const Value* FindPathOfType(std::initializer_list<StringPiece> path,
                              Type type) const;
  const Value* FindPathOfType(span<const StringPiece> path, Type type) const;

  // Sets the given path, expanding and creating dictionary keys as necessary.
  //
  // If the current value is not a dictionary, the function returns nullptr. If
  // path components do not exist, they will be created. If any but the last
  // components matches a value that is not a dictionary, the function will fail
  // (it will not overwrite the value) and return nullptr. The last path
  // component will be unconditionally overwritten if it exists, and created if
  // it doesn't.
  //
  // Example:
  //   value.SetPath({"foo", "bar"}, std::move(myvalue));
  //
  //   std::vector<StringPiece> components = ...
  //   value.SetPath(components, std::move(myvalue));
  Value* SetPath(std::initializer_list<StringPiece> path, Value value);
  Value* SetPath(span<const StringPiece> path, Value value);

  // Tries to remove a Value at the given path.
  //
  // If the current value is not a dictionary or any path components does not
  // exist, this operation fails, leaves underlying Values untouched and returns
  // |false|. In case intermediate dictionaries become empty as a result of this
  // path removal, they will be removed as well.
  //
  // Example:
  //   bool success = value.RemovePath({"foo", "bar"});
  //
  //   std::vector<StringPiece> components = ...
  //   bool success = value.RemovePath(components);
  bool RemovePath(std::initializer_list<StringPiece> path);
  bool RemovePath(span<const StringPiece> path);

  using dict_iterator_proxy = detail::dict_iterator_proxy;
  using const_dict_iterator_proxy = detail::const_dict_iterator_proxy;

  // |DictItems| returns a proxy object that exposes iterators to the underlying
  // dictionary. These are intended for iteration over all items in the
  // dictionary and are compatible with for-each loops and standard library
  // algorithms.
  // Note: This fatally asserts if type() is not Type::DICTIONARY.
  dict_iterator_proxy DictItems();
  const_dict_iterator_proxy DictItems() const;

  // These methods allow the convenient retrieval of the contents of the Value.
  // If the current object can be converted into the given type, the value is
  // returned through the |out_value| parameter and true is returned;
  // otherwise, false is returned and |out_value| is unchanged.
  // DEPRECATED, use GetBool() instead.
  bool GetAsBoolean(bool* out_value) const;
  // DEPRECATED, use GetInt() instead.
  bool GetAsInteger(int* out_value) const;
  // DEPRECATED, use GetDouble() instead.
  bool GetAsDouble(double* out_value) const;
  // DEPRECATED, use GetString() instead.
  bool GetAsString(std::string* out_value) const;
  bool GetAsString(string16* out_value) const;
  bool GetAsString(const Value** out_value) const;
  bool GetAsString(StringPiece* out_value) const;
  // ListValue::From is the equivalent for std::unique_ptr conversions.
  // DEPRECATED, use GetList() instead.
  bool GetAsList(ListValue** out_value);
  bool GetAsList(const ListValue** out_value) const;
  // DictionaryValue::From is the equivalent for std::unique_ptr conversions.
  bool GetAsDictionary(DictionaryValue** out_value);
  bool GetAsDictionary(const DictionaryValue** out_value) const;
  // Note: Do not add more types. See the file-level comment above for why.

  // This creates a deep copy of the entire Value tree, and returns a pointer
  // to the copy. The caller gets ownership of the copy, of course.
  // Subclasses return their own type directly in their overrides;
  // this works because C++ supports covariant return types.
  // DEPRECATED, use Value::Clone() instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  Value* DeepCopy() const;
  // DEPRECATED, use Value::Clone() instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  std::unique_ptr<Value> CreateDeepCopy() const;

  // Comparison operators so that Values can easily be used with standard
  // library algorithms and associative containers.
  BASE_EXPORT friend bool operator==(const Value& lhs, const Value& rhs);
  BASE_EXPORT friend bool operator!=(const Value& lhs, const Value& rhs);
  BASE_EXPORT friend bool operator<(const Value& lhs, const Value& rhs);
  BASE_EXPORT friend bool operator>(const Value& lhs, const Value& rhs);
  BASE_EXPORT friend bool operator<=(const Value& lhs, const Value& rhs);
  BASE_EXPORT friend bool operator>=(const Value& lhs, const Value& rhs);

  // Compares if two Value objects have equal contents.
  // DEPRECATED, use operator==(const Value& lhs, const Value& rhs) instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  bool Equals(const Value* other) const;

 protected:
  // TODO(crbug.com/646113): Make these private once DictionaryValue and
  // ListValue are properly inlined.
  Type type_;

  union {
    bool bool_value_;
    int int_value_;
    double double_value_;
    std::string string_value_;
    BlobStorage binary_value_;
    DictStorage dict_;
    ListStorage list_;
  };

 private:
  void InternalMoveConstructFrom(Value&& that);
  void InternalCleanup();

  DISALLOW_COPY_AND_ASSIGN(Value);
};

// DictionaryValue provides a key-value dictionary with (optional) "path"
// parsing for recursive access; see the comment at the top of the file. Keys
// are |std::string|s and should be UTF-8 encoded.
class BASE_EXPORT DictionaryValue : public Value {
 public:
  using const_iterator = DictStorage::const_iterator;
  using iterator = DictStorage::iterator;

  // Returns |value| if it is a dictionary, nullptr otherwise.
  static std::unique_ptr<DictionaryValue> From(std::unique_ptr<Value> value);

  DictionaryValue();
  explicit DictionaryValue(const DictStorage& in_dict);
  explicit DictionaryValue(DictStorage&& in_dict) noexcept;

  // Returns true if the current dictionary has a value for the given key.
  // DEPRECATED, use Value::FindKey(key) instead.
  bool HasKey(StringPiece key) const;

  // Returns the number of Values in this dictionary.
  size_t size() const { return dict_.size(); }

  // Returns whether the dictionary is empty.
  bool empty() const { return dict_.empty(); }

  // Clears any current contents of this dictionary.
  void Clear();

  // Sets the Value associated with the given path starting from this object.
  // A path has the form "<key>" or "<key>.<key>.[...]", where "." indexes
  // into the next DictionaryValue down.  Obviously, "." can't be used
  // within a key, but there are no other restrictions on keys.
  // If the key at any step of the way doesn't exist, or exists but isn't
  // a DictionaryValue, a new DictionaryValue will be created and attached
  // to the path in that location. |in_value| must be non-null.
  // Returns a pointer to the inserted value.
  // DEPRECATED, use Value::SetPath(path, value) instead.
  Value* Set(StringPiece path, std::unique_ptr<Value> in_value);

  // Convenience forms of Set().  These methods will replace any existing
  // value at that path, even if it has a different type.
  // DEPRECATED, use Value::SetPath(path, Value(bool)) instead.
  Value* SetBoolean(StringPiece path, bool in_value);
  // DEPRECATED, use Value::SetPath(path, Value(int)) instead.
  Value* SetInteger(StringPiece path, int in_value);
  // DEPRECATED, use Value::SetPath(path, Value(double)) instead.
  Value* SetDouble(StringPiece path, double in_value);
  // DEPRECATED, use Value::SetPath(path, Value(StringPiece)) instead.
  Value* SetString(StringPiece path, StringPiece in_value);
  // DEPRECATED, use Value::SetPath(path, Value(const string& 16)) instead.
  Value* SetString(StringPiece path, const string16& in_value);
  // DEPRECATED, use Value::SetPath(path, Value(Type::DICTIONARY)) instead.
  DictionaryValue* SetDictionary(StringPiece path,
                                 std::unique_ptr<DictionaryValue> in_value);
  // DEPRECATED, use Value::SetPath(path, Value(Type::LIST)) instead.
  ListValue* SetList(StringPiece path, std::unique_ptr<ListValue> in_value);

  // Like Set(), but without special treatment of '.'.  This allows e.g. URLs to
  // be used as paths.
  // DEPRECATED, use Value::SetKey(key, value) instead.
  Value* SetWithoutPathExpansion(StringPiece key,
                                 std::unique_ptr<Value> in_value);

  // Convenience forms of SetWithoutPathExpansion().
  // DEPRECATED, use Value::SetKey(key, Value(Type::DICTIONARY)) instead.
  DictionaryValue* SetDictionaryWithoutPathExpansion(
      StringPiece path,
      std::unique_ptr<DictionaryValue> in_value);
  // DEPRECATED, use Value::SetKey(key, Value(Type::LIST)) instead.
  ListValue* SetListWithoutPathExpansion(StringPiece path,
                                         std::unique_ptr<ListValue> in_value);

  // Gets the Value associated with the given path starting from this object.
  // A path has the form "<key>" or "<key>.<key>.[...]", where "." indexes
  // into the next DictionaryValue down.  If the path can be resolved
  // successfully, the value for the last key in the path will be returned
  // through the |out_value| parameter, and the function will return true.
  // Otherwise, it will return false and |out_value| will be untouched.
  // Note that the dictionary always owns the value that's returned.
  // |out_value| is optional and will only be set if non-NULL.
  // DEPRECATED, use Value::FindPath(path) instead.
  bool Get(StringPiece path, const Value** out_value) const;
  // DEPRECATED, use Value::FindPath(path) instead.
  bool Get(StringPiece path, Value** out_value);

  // These are convenience forms of Get().  The value will be retrieved
  // and the return value will be true if the path is valid and the value at
  // the end of the path can be returned in the form specified.
  // |out_value| is optional and will only be set if non-NULL.
  // DEPRECATED, use Value::FindPath(path) and Value::GetBool() instead.
  bool GetBoolean(StringPiece path, bool* out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetInt() instead.
  bool GetInteger(StringPiece path, int* out_value) const;
  // Values of both type Type::INTEGER and Type::DOUBLE can be obtained as
  // doubles.
  // DEPRECATED, use Value::FindPath(path) and Value::GetDouble() instead.
  bool GetDouble(StringPiece path, double* out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetString() instead.
  bool GetString(StringPiece path, std::string* out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetString() instead.
  bool GetString(StringPiece path, string16* out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetString() instead.
  bool GetStringASCII(StringPiece path, std::string* out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetBlob() instead.
  bool GetBinary(StringPiece path, const Value** out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetBlob() instead.
  bool GetBinary(StringPiece path, Value** out_value);
  // DEPRECATED, use Value::FindPath(path) and Value's Dictionary API instead.
  bool GetDictionary(StringPiece path,
                     const DictionaryValue** out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value's Dictionary API instead.
  bool GetDictionary(StringPiece path, DictionaryValue** out_value);
  // DEPRECATED, use Value::FindPath(path) and Value::GetList() instead.
  bool GetList(StringPiece path, const ListValue** out_value) const;
  // DEPRECATED, use Value::FindPath(path) and Value::GetList() instead.
  bool GetList(StringPiece path, ListValue** out_value);

  // Like Get(), but without special treatment of '.'.  This allows e.g. URLs to
  // be used as paths.
  // DEPRECATED, use Value::FindKey(key) instead.
  bool GetWithoutPathExpansion(StringPiece key, const Value** out_value) const;
  // DEPRECATED, use Value::FindKey(key) instead.
  bool GetWithoutPathExpansion(StringPiece key, Value** out_value);
  // DEPRECATED, use Value::FindKey(key) and Value::GetBool() instead.
  bool GetBooleanWithoutPathExpansion(StringPiece key, bool* out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value::GetInt() instead.
  bool GetIntegerWithoutPathExpansion(StringPiece key, int* out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value::GetDouble() instead.
  bool GetDoubleWithoutPathExpansion(StringPiece key, double* out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value::GetString() instead.
  bool GetStringWithoutPathExpansion(StringPiece key,
                                     std::string* out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value::GetString() instead.
  bool GetStringWithoutPathExpansion(StringPiece key,
                                     string16* out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value's Dictionary API instead.
  bool GetDictionaryWithoutPathExpansion(
      StringPiece key,
      const DictionaryValue** out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value's Dictionary API instead.
  bool GetDictionaryWithoutPathExpansion(StringPiece key,
                                         DictionaryValue** out_value);
  // DEPRECATED, use Value::FindKey(key) and Value::GetList() instead.
  bool GetListWithoutPathExpansion(StringPiece key,
                                   const ListValue** out_value) const;
  // DEPRECATED, use Value::FindKey(key) and Value::GetList() instead.
  bool GetListWithoutPathExpansion(StringPiece key, ListValue** out_value);

  // Removes the Value with the specified path from this dictionary (or one
  // of its child dictionaries, if the path is more than just a local key).
  // If |out_value| is non-NULL, the removed Value will be passed out via
  // |out_value|.  If |out_value| is NULL, the removed value will be deleted.
  // This method returns true if |path| is a valid path; otherwise it will
  // return false and the DictionaryValue object will be unchanged.
  // DEPRECATED, use Value::RemovePath(path) instead.
  bool Remove(StringPiece path, std::unique_ptr<Value>* out_value);

  // Like Remove(), but without special treatment of '.'.  This allows e.g. URLs
  // to be used as paths.
  // DEPRECATED, use Value::RemoveKey(key) instead.
  bool RemoveWithoutPathExpansion(StringPiece key,
                                  std::unique_ptr<Value>* out_value);

  // Removes a path, clearing out all dictionaries on |path| that remain empty
  // after removing the value at |path|.
  // DEPRECATED, use Value::RemovePath(path) instead.
  bool RemovePath(StringPiece path, std::unique_ptr<Value>* out_value);

  using Value::RemovePath;  // DictionaryValue::RemovePath shadows otherwise.

  // Makes a copy of |this| but doesn't include empty dictionaries and lists in
  // the copy.  This never returns NULL, even if |this| itself is empty.
  std::unique_ptr<DictionaryValue> DeepCopyWithoutEmptyChildren() const;

  // Merge |dictionary| into this dictionary. This is done recursively, i.e. any
  // sub-dictionaries will be merged as well. In case of key collisions, the
  // passed in dictionary takes precedence and data already present will be
  // replaced. Values within |dictionary| are deep-copied, so |dictionary| may
  // be freed any time after this call.
  void MergeDictionary(const DictionaryValue* dictionary);

  // Swaps contents with the |other| dictionary.
  void Swap(DictionaryValue* other);

  // This class provides an iterator over both keys and values in the
  // dictionary.  It can't be used to modify the dictionary.
  // DEPRECATED, use Value::DictItems() instead.
  class BASE_EXPORT Iterator {
   public:
    explicit Iterator(const DictionaryValue& target);
    Iterator(const Iterator& other);
    ~Iterator();

    bool IsAtEnd() const { return it_ == target_.dict_.end(); }
    void Advance() { ++it_; }

    const std::string& key() const { return it_->first; }
    const Value& value() const { return *it_->second; }

   private:
    const DictionaryValue& target_;
    DictStorage::const_iterator it_;
  };

  // Iteration.
  // DEPRECATED, use Value::DictItems() instead.
  iterator begin() { return dict_.begin(); }
  iterator end() { return dict_.end(); }

  // DEPRECATED, use Value::DictItems() instead.
  const_iterator begin() const { return dict_.begin(); }
  const_iterator end() const { return dict_.end(); }

  // DEPRECATED, use Value::Clone() instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  DictionaryValue* DeepCopy() const;
  // DEPRECATED, use Value::Clone() instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  std::unique_ptr<DictionaryValue> CreateDeepCopy() const;
};

// This type of Value represents a list of other Value values.
class BASE_EXPORT ListValue : public Value {
 public:
  using const_iterator = ListStorage::const_iterator;
  using iterator = ListStorage::iterator;

  // Returns |value| if it is a list, nullptr otherwise.
  static std::unique_ptr<ListValue> From(std::unique_ptr<Value> value);

  ListValue();
  explicit ListValue(const ListStorage& in_list);
  explicit ListValue(ListStorage&& in_list) noexcept;

  // Clears the contents of this ListValue
  // DEPRECATED, use GetList()::clear() instead.
  void Clear();

  // Returns the number of Values in this list.
  // DEPRECATED, use GetList()::size() instead.
  size_t GetSize() const { return list_.size(); }

  // Returns the capacity of storage for Values in this list.
  // DEPRECATED, use GetList()::capacity() instead.
  size_t capacity() const { return list_.capacity(); }

  // Returns whether the list is empty.
  // DEPRECATED, use GetList()::empty() instead.
  bool empty() const { return list_.empty(); }

  // Reserves storage for at least |n| values.
  // DEPRECATED, use GetList()::reserve() instead.
  void Reserve(size_t n);

  // Sets the list item at the given index to be the Value specified by
  // the value given.  If the index beyond the current end of the list, null
  // Values will be used to pad out the list.
  // Returns true if successful, or false if the index was negative or
  // the value is a null pointer.
  // DEPRECATED, use GetList()::operator[] instead.
  bool Set(size_t index, std::unique_ptr<Value> in_value);

  // Gets the Value at the given index.  Modifies |out_value| (and returns true)
  // only if the index falls within the current list range.
  // Note that the list always owns the Value passed out via |out_value|.
  // |out_value| is optional and will only be set if non-NULL.
  // DEPRECATED, use GetList()::operator[] instead.
  bool Get(size_t index, const Value** out_value) const;
  bool Get(size_t index, Value** out_value);

  // Convenience forms of Get().  Modifies |out_value| (and returns true)
  // only if the index is valid and the Value at that index can be returned
  // in the specified form.
  // |out_value| is optional and will only be set if non-NULL.
  // DEPRECATED, use GetList()::operator[]::GetBool() instead.
  bool GetBoolean(size_t index, bool* out_value) const;
  // DEPRECATED, use GetList()::operator[]::GetInt() instead.
  bool GetInteger(size_t index, int* out_value) const;
  // Values of both type Type::INTEGER and Type::DOUBLE can be obtained as
  // doubles.
  // DEPRECATED, use GetList()::operator[]::GetDouble() instead.
  bool GetDouble(size_t index, double* out_value) const;
  // DEPRECATED, use GetList()::operator[]::GetString() instead.
  bool GetString(size_t index, std::string* out_value) const;
  bool GetString(size_t index, string16* out_value) const;
  // DEPRECATED, use GetList()::operator[]::GetBlob() instead.
  bool GetBinary(size_t index, const Value** out_value) const;
  bool GetBinary(size_t index, Value** out_value);

  bool GetDictionary(size_t index, const DictionaryValue** out_value) const;
  bool GetDictionary(size_t index, DictionaryValue** out_value);

  using Value::GetList;
  // DEPRECATED, use GetList()::operator[]::GetList() instead.
  bool GetList(size_t index, const ListValue** out_value) const;
  bool GetList(size_t index, ListValue** out_value);

  // Removes the Value with the specified index from this list.
  // If |out_value| is non-NULL, the removed Value AND ITS OWNERSHIP will be
  // passed out via |out_value|.  If |out_value| is NULL, the removed value will
  // be deleted.  This method returns true if |index| is valid; otherwise
  // it will return false and the ListValue object will be unchanged.
  // DEPRECATED, use GetList()::erase() instead.
  bool Remove(size_t index, std::unique_ptr<Value>* out_value);

  // Removes the first instance of |value| found in the list, if any, and
  // deletes it. |index| is the location where |value| was found. Returns false
  // if not found.
  // DEPRECATED, use GetList()::erase() instead.
  bool Remove(const Value& value, size_t* index);

  // Removes the element at |iter|. If |out_value| is NULL, the value will be
  // deleted, otherwise ownership of the value is passed back to the caller.
  // Returns an iterator pointing to the location of the element that
  // followed the erased element.
  // DEPRECATED, use GetList()::erase() instead.
  iterator Erase(iterator iter, std::unique_ptr<Value>* out_value);

  // Appends a Value to the end of the list.
  // DEPRECATED, use GetList()::push_back() instead.
  void Append(std::unique_ptr<Value> in_value);

  // Convenience forms of Append.
  // DEPRECATED, use GetList()::emplace_back() instead.
  void AppendBoolean(bool in_value);
  void AppendInteger(int in_value);
  void AppendDouble(double in_value);
  void AppendString(StringPiece in_value);
  void AppendString(const string16& in_value);
  // DEPRECATED, use GetList()::emplace_back() in a loop instead.
  void AppendStrings(const std::vector<std::string>& in_values);
  void AppendStrings(const std::vector<string16>& in_values);

  // Appends a Value if it's not already present. Returns true if successful,
  // or false if the value was already
  // DEPRECATED, use std::find() with GetList()::push_back() instead.
  bool AppendIfNotPresent(std::unique_ptr<Value> in_value);

  // Insert a Value at index.
  // Returns true if successful, or false if the index was out of range.
  // DEPRECATED, use GetList()::insert() instead.
  bool Insert(size_t index, std::unique_ptr<Value> in_value);

  // Searches for the first instance of |value| in the list using the Equals
  // method of the Value type.
  // Returns a const_iterator to the found item or to end() if none exists.
  // DEPRECATED, use std::find() instead.
  const_iterator Find(const Value& value) const;

  // Swaps contents with the |other| list.
  // DEPRECATED, use GetList()::swap() instead.
  void Swap(ListValue* other);

  // Iteration.
  // DEPRECATED, use GetList()::begin() instead.
  iterator begin() { return list_.begin(); }
  // DEPRECATED, use GetList()::end() instead.
  iterator end() { return list_.end(); }

  // DEPRECATED, use GetList()::begin() instead.
  const_iterator begin() const { return list_.begin(); }
  // DEPRECATED, use GetList()::end() instead.
  const_iterator end() const { return list_.end(); }

  // DEPRECATED, use Value::Clone() instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  ListValue* DeepCopy() const;
  // DEPRECATED, use Value::Clone() instead.
  // TODO(crbug.com/646113): Delete this and migrate callsites.
  std::unique_ptr<ListValue> CreateDeepCopy() const;
};

// This interface is implemented by classes that know how to serialize
// Value objects.
class BASE_EXPORT ValueSerializer {
 public:
  virtual ~ValueSerializer();

  virtual bool Serialize(const Value& root) = 0;
};

// This interface is implemented by classes that know how to deserialize Value
// objects.
class BASE_EXPORT ValueDeserializer {
 public:
  virtual ~ValueDeserializer();

  // This method deserializes the subclass-specific format into a Value object.
  // If the return value is non-NULL, the caller takes ownership of returned
  // Value. If the return value is NULL, and if error_code is non-NULL,
  // error_code will be set with the underlying error.
  // If |error_message| is non-null, it will be filled in with a formatted
  // error message including the location of the error if appropriate.
  virtual std::unique_ptr<Value> Deserialize(int* error_code,
                                             std::string* error_str) = 0;
};

// Stream operator so Values can be used in assertion statements.  In order that
// gtest uses this operator to print readable output on test failures, we must
// override each specific type. Otherwise, the default template implementation
// is preferred over an upcast.
BASE_EXPORT std::ostream& operator<<(std::ostream& out, const Value& value);

BASE_EXPORT inline std::ostream& operator<<(std::ostream& out,
                                            const DictionaryValue& value) {
  return out << static_cast<const Value&>(value);
}

BASE_EXPORT inline std::ostream& operator<<(std::ostream& out,
                                            const ListValue& value) {
  return out << static_cast<const Value&>(value);
}

// Stream operator so that enum class Types can be used in log statements.
BASE_EXPORT std::ostream& operator<<(std::ostream& out,
                                     const Value::Type& type);

}  // namespace base

#endif  // BASE_VALUES_H_
