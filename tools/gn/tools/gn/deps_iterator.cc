// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/deps_iterator.h"

#include "tools/gn/target.h"

DepsIterator::DepsIterator() : current_index_(0) {
  vect_stack_[0] = nullptr;
  vect_stack_[1] = nullptr;
  vect_stack_[2] = nullptr;
}

DepsIterator::DepsIterator(const LabelTargetVector* a,
                           const LabelTargetVector* b,
                           const LabelTargetVector* c)
    : current_index_(0) {
  vect_stack_[0] = a;
  vect_stack_[1] = b;
  vect_stack_[2] = c;

  if (vect_stack_[0] && vect_stack_[0]->empty())
    operator++();
}

// Advance to the next position. This assumes there are more vectors.
//
// For internal use, this function tolerates an initial index equal to the
// length of the current vector. In this case, it will advance to the next
// one.
DepsIterator& DepsIterator::operator++() {
  DCHECK(vect_stack_[0]);

  current_index_++;
  if (current_index_ >= vect_stack_[0]->size()) {
    // Advance to next vect. Shift the elements left by one.
    vect_stack_[0] = vect_stack_[1];
    vect_stack_[1] = vect_stack_[2];
    vect_stack_[2] = nullptr;

    current_index_ = 0;

    if (vect_stack_[0] && vect_stack_[0]->empty())
      operator++();
  }
  return *this;
}

DepsIteratorRange::DepsIteratorRange(const DepsIterator& b)
    : begin_(b), end_() {}

DepsIteratorRange::~DepsIteratorRange() = default;
