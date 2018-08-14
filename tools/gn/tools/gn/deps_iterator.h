// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_DEPS_ITERATOR_H_
#define TOOLS_GN_DEPS_ITERATOR_H_

#include <stddef.h>

#include "tools/gn/label_ptr.h"

// Provides an iterator for iterating over multiple LabelTargetVectors to
// make it convenient to iterate over all deps of a target.
//
// This works by maintaining a simple stack of vectors (since we have a fixed
// number of deps types). When the stack is empty, we've reached the end. This
// means that the default-constructed iterator == end() for any sequence.
class DepsIterator {
 public:
  // Creates an empty iterator.
  DepsIterator();

  // Iterate over the deps in the given vectors. If passing less than three,
  // pad with nulls.
  DepsIterator(const LabelTargetVector* a,
               const LabelTargetVector* b,
               const LabelTargetVector* c);

  // Prefix increment operator. This assumes there are more items (i.e.
  // *this != DepsIterator()).
  //
  // For internal use, this function tolerates an initial index equal to the
  // length of the current vector. In this case, it will advance to the next
  // one.
  DepsIterator& operator++();

  // Comparison for STL-based loops.
  bool operator!=(const DepsIterator& other) const {
    return current_index_ != other.current_index_ ||
           vect_stack_[0] != other.vect_stack_[0] ||
           vect_stack_[1] != other.vect_stack_[1] ||
           vect_stack_[2] != other.vect_stack_[2];
  }

  // Dereference operator for STL-compatible iterators.
  const LabelTargetPair& operator*() const {
    DCHECK_LT(current_index_, vect_stack_[0]->size());
    return (*vect_stack_[0])[current_index_];
  }

 private:
  const LabelTargetVector* vect_stack_[3];

  size_t current_index_;
};

// Provides a virtual container implementing begin() and end() for a
// sequence of deps. This can then be used in range-based for loops.
class DepsIteratorRange {
 public:
  explicit DepsIteratorRange(const DepsIterator& b);
  ~DepsIteratorRange();

  const DepsIterator& begin() const { return begin_; }
  const DepsIterator& end() const { return end_; }

 private:
  DepsIterator begin_;
  DepsIterator end_;
};

#endif  // TOOLS_GN_DEPS_ITERATOR_H_
