/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_BASE_INTRUSIVE_LIST_H_
#define SRC_BASE_INTRUSIVE_LIST_H_

#include <cstddef>
#include <cstdint>

#include "perfetto/base/logging.h"

// An intrusive (doubly linked) list implementation.
// Unlike std::list<>, the entries being inserted into the list need to
// explicitly declare an IntrusiveListNode structure (one for each list they are
// part of). The user must specify a Traits struct for each list the entry is
// part of. The traits struct defines how to get to the IntrusiveListNode from
// the outer object.
//
// Usage example:
// class Person {
//  public:
//   struct ListTraits {
//     static constexpr size_t node_offset() { return offsetof(Person, node); }
//   };
//   std::string name;
//   IntrusiveListNode node{};
// }
//
// IntrusiveList<Person, Person::ListTraits> list;
// Person person;
// list.PushBack(person);
// ...

namespace perfetto::base {

namespace internal {

struct ListNode {
  ListNode* prev;
  ListNode* next;
};

// IntrusiveList's Base class to factor out type-independent code (avoid binary
// bloat)
class ListOps {
 public:
  void PushFront(ListNode* node);
  void PopFront();
  void Erase(ListNode* node);

  ListNode* front_{nullptr};
  size_t size_{0};
};

}  // namespace internal

using IntrusiveListNode = internal::ListNode;

// T is the class that has one or more IntrusiveListNode as fields.
// Traits defines getter and offset between node and T.
// Traits is separate to allow the same T to be part of different lists (which
// necessitate a different Traits, at very least for the offset).
template <typename T, typename ListTraits>
class IntrusiveList : private internal::ListOps {
 public:
  class Iterator {
   public:
    Iterator() = default;
    explicit Iterator(IntrusiveListNode* node) : node_(node) {}
    ~Iterator() = default;
    Iterator(const Iterator&) = default;
    Iterator& operator=(const Iterator&) = default;
    Iterator(Iterator&&) noexcept = default;
    Iterator& operator=(Iterator&&) noexcept = default;

    bool operator==(const Iterator& other) const {
      return node_ == other.node_;
    }
    bool operator!=(const Iterator& other) const { return !(*this == other); }
    T* operator->() { return const_cast<T*>(entryof(node_)); }
    T& operator*() {
      PERFETTO_DCHECK(node_);
      return *operator->();
    }
    explicit operator bool() const { return node_ != nullptr; }

    Iterator& operator++() {
      PERFETTO_DCHECK(node_);
      node_ = node_->next;
      return *this;
    }

   private:
    IntrusiveListNode* node_{nullptr};
  };

  using value_type = T;
  using const_pointer = const T*;

  void PushFront(T& entry) { internal::ListOps::PushFront(nodeof(&entry)); }

  void PopFront() { internal::ListOps::PopFront(); }

  T& front() {
    PERFETTO_DCHECK(front_);
    return const_cast<T&>(*entryof(front_));
  }

  void Erase(T& entry) { internal::ListOps::Erase(nodeof(&entry)); }

  bool empty() const { return size_ == 0; }

  size_t size() const { return size_; }

  Iterator begin() { return Iterator{front_}; }

  Iterator end() { return Iterator{nullptr}; }

 private:
  static constexpr size_t kNodeOffset = ListTraits::node_offset();

  static constexpr IntrusiveListNode* nodeof(T* entry) {
    return reinterpret_cast<IntrusiveListNode*>(
        reinterpret_cast<uintptr_t>(entry) + kNodeOffset);
  }

  static constexpr const T* entryof(IntrusiveListNode* node) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(node) -
                                kNodeOffset);
  }
};

}  // namespace perfetto::base

#endif  // SRC_BASE_INTRUSIVE_LIST_H_
