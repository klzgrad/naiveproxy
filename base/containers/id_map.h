// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_ID_MAP_H_
#define BASE_CONTAINERS_ID_MAP_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "base/containers/flat_set.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/sequence_checker.h"

namespace base {

// This object maintains a list of IDs that can be quickly converted to
// pointers to objects. It is implemented as a hash table, optimized for
// relatively small data sets (in the common case, there will be exactly one
// item in the list).
//
// Items can be inserted into the container with arbitrary ID, but the caller
// must ensure they are unique. Inserting IDs and relying on automatically
// generated ones is not allowed because they can collide.

// The map's value type (the V param) can be any dereferenceable type, such as a
// raw pointer or smart pointer
template <typename V, typename K = int32_t>
class IDMap final {
 public:
  using KeyType = K;

 private:
  using T = typename std::remove_reference<decltype(*V())>::type;

  using HashTable = std::unordered_map<KeyType, V>;

 public:
  IDMap() : iteration_depth_(0), next_id_(1), check_on_null_data_(false) {
    // A number of consumers of IDMap create it on one thread but always
    // access it from a different, but consistent, thread (or sequence)
    // post-construction. The first call to CalledOnValidSequence() will re-bind
    // it.
    sequence_checker_.DetachFromSequence();
  }

  ~IDMap() {
    // Many IDMap's are static, and hence will be destroyed on the main
    // thread. However, all the accesses may take place on another thread (or
    // sequence), such as the IO thread. Detaching again to clean this up.
    sequence_checker_.DetachFromSequence();
  }

  // Sets whether Add and Replace should DCHECK if passed in NULL data.
  // Default is false.
  void set_check_on_null_data(bool value) { check_on_null_data_ = value; }

  // Adds a view with an automatically generated unique ID. See AddWithID.
  KeyType Add(V data) { return AddInternal(std::move(data)); }

  // Adds a new data member with the specified ID. The ID must not be in
  // the list. The caller either must generate all unique IDs itself and use
  // this function, or allow this object to generate IDs and call Add. These
  // two methods may not be mixed, or duplicate IDs may be generated.
  void AddWithID(V data, KeyType id) { AddWithIDInternal(std::move(data), id); }

  void Remove(KeyType id) {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    typename HashTable::iterator i = data_.find(id);
    if (i == data_.end()) {
      NOTREACHED() << "Attempting to remove an item not in the list";
      return;
    }

    if (iteration_depth_ == 0) {
      data_.erase(i);
    } else {
      removed_ids_.insert(id);
    }
  }

  // Replaces the value for |id| with |new_data| and returns the existing value.
  // Should only be called with an already added id.
  V Replace(KeyType id, V new_data) {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    DCHECK(!check_on_null_data_ || new_data);
    typename HashTable::iterator i = data_.find(id);
    DCHECK(i != data_.end());

    std::swap(i->second, new_data);
    return new_data;
  }

  void Clear() {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    if (iteration_depth_ == 0) {
      data_.clear();
    } else {
      for (typename HashTable::iterator i = data_.begin();
           i != data_.end(); ++i)
        removed_ids_.insert(i->first);
    }
  }

  bool IsEmpty() const {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    return size() == 0u;
  }

  T* Lookup(KeyType id) const {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    typename HashTable::const_iterator i = data_.find(id);
    if (i == data_.end())
      return nullptr;
    return &*i->second;
  }

  size_t size() const {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    return data_.size() - removed_ids_.size();
  }

#if defined(UNIT_TEST)
  int iteration_depth() const {
    return iteration_depth_;
  }
#endif  // defined(UNIT_TEST)

  // It is safe to remove elements from the map during iteration. All iterators
  // will remain valid.
  template<class ReturnType>
  class Iterator {
   public:
    Iterator(IDMap<V, K>* map) : map_(map), iter_(map_->data_.begin()) {
      Init();
    }

    Iterator(const Iterator& iter)
        : map_(iter.map_),
          iter_(iter.iter_) {
      Init();
    }

    const Iterator& operator=(const Iterator& iter) {
      map_ = iter.map;
      iter_ = iter.iter;
      Init();
      return *this;
    }

    ~Iterator() {
      DCHECK(map_->sequence_checker_.CalledOnValidSequence());

      // We're going to decrement iteration depth. Make sure it's greater than
      // zero so that it doesn't become negative.
      DCHECK_LT(0, map_->iteration_depth_);

      if (--map_->iteration_depth_ == 0)
        map_->Compact();
    }

    bool IsAtEnd() const {
      DCHECK(map_->sequence_checker_.CalledOnValidSequence());
      return iter_ == map_->data_.end();
    }

    KeyType GetCurrentKey() const {
      DCHECK(map_->sequence_checker_.CalledOnValidSequence());
      return iter_->first;
    }

    ReturnType* GetCurrentValue() const {
      DCHECK(map_->sequence_checker_.CalledOnValidSequence());
      return &*iter_->second;
    }

    void Advance() {
      DCHECK(map_->sequence_checker_.CalledOnValidSequence());
      ++iter_;
      SkipRemovedEntries();
    }

   private:
    void Init() {
      DCHECK(map_->sequence_checker_.CalledOnValidSequence());
      ++map_->iteration_depth_;
      SkipRemovedEntries();
    }

    void SkipRemovedEntries() {
      while (iter_ != map_->data_.end() &&
             map_->removed_ids_.find(iter_->first) !=
             map_->removed_ids_.end()) {
        ++iter_;
      }
    }

    IDMap<V, K>* map_;
    typename HashTable::const_iterator iter_;
  };

  typedef Iterator<T> iterator;
  typedef Iterator<const T> const_iterator;

 private:
  KeyType AddInternal(V data) {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    DCHECK(!check_on_null_data_ || data);
    KeyType this_id = next_id_;
    DCHECK(data_.find(this_id) == data_.end()) << "Inserting duplicate item";
    data_[this_id] = std::move(data);
    next_id_++;
    return this_id;
  }

  void AddWithIDInternal(V data, KeyType id) {
    DCHECK(sequence_checker_.CalledOnValidSequence());
    DCHECK(!check_on_null_data_ || data);
    DCHECK(data_.find(id) == data_.end()) << "Inserting duplicate item";
    data_[id] = std::move(data);
  }

  void Compact() {
    DCHECK_EQ(0, iteration_depth_);
    for (const auto& i : removed_ids_)
      Remove(i);
    removed_ids_.clear();
  }

  // Keep track of how many iterators are currently iterating on us to safely
  // handle removing items during iteration.
  int iteration_depth_;

  // Keep set of IDs that should be removed after the outermost iteration has
  // finished. This way we manage to not invalidate the iterator when an element
  // is removed.
  base::flat_set<KeyType> removed_ids_;

  // The next ID that we will return from Add()
  KeyType next_id_;

  HashTable data_;

  // See description above setter.
  bool check_on_null_data_;

  base::SequenceChecker sequence_checker_;

  DISALLOW_COPY_AND_ASSIGN(IDMap);
};

}  // namespace base

#endif  // BASE_CONTAINERS_ID_MAP_H_
