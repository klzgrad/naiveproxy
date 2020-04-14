// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_SPDY_INTRUSIVE_LIST_H_
#define QUICHE_SPDY_CORE_SPDY_INTRUSIVE_LIST_H_

// A SpdyIntrusiveList<> is a doubly-linked list where the link pointers are
// embedded in the elements. They are circularly linked making insertion and
// removal into a known position constant time and branch-free operations.
//
// Usage is similar to an STL list<> where feasible, but there are important
// differences. First and foremost, the elements must derive from the
// SpdyIntrusiveLink<> base class:
//
//   struct Foo : public SpdyIntrusiveLink<Foo> {
//     // ...
//   }
//
//   SpdyIntrusiveList<Foo> l;
//   l.push_back(new Foo);
//   l.push_front(new Foo);
//   l.erase(&l.front());
//   l.erase(&l.back());
//
// Intrusive lists are primarily useful when you would have considered embedding
// link pointers in your class directly for space or performance reasons. An
// SpdyIntrusiveLink<> is the size of 2 pointers, usually 16 bytes on 64-bit
// systems. Intrusive lists do not perform memory allocation (unlike the STL
// list<> class) and thus may use less memory than list<>. In particular, if the
// list elements are pointers to objects, using a list<> would perform an extra
// memory allocation for each list node structure, while an SpdyIntrusiveList<>
// would not.
//
// Note that SpdyIntrusiveLink is exempt from the C++ style guide's limitations
// on multiple inheritance, so it's fine to inherit from both SpdyIntrusiveLink
// and a base class, even if the base class is not a pure interface.
//
// Because the list pointers are embedded in the objects stored in an
// SpdyIntrusiveList<>, erasing an item from a list is constant time. Consider
// the following:
//
//   map<string,Foo> foo_map;
//   list<Foo*> foo_list;
//
//   foo_list.push_back(&foo_map["bar"]);
//   foo_list.erase(&foo_map["bar"]); // Compile error!
//
// The problem here is that a Foo* doesn't know where on foo_list it resides,
// so removal requires iteration over the list. Various tricks can be performed
// to overcome this. For example, a foo_list::iterator can be stored inside of
// the Foo object. But at that point you'd be better off using an
// SpdyIntrusiveList<>:
//
//   map<string,Foo> foo_map;
//   SpdyIntrusiveList<Foo> foo_list;
//
//   foo_list.push_back(&foo_map["bar"]);
//   foo_list.erase(&foo_map["bar"]); // Yeah!
//
// Note that SpdyIntrusiveLists come with a few limitations. The primary
// limitation is that the SpdyIntrusiveLink<> base class is not copyable or
// assignable. The result is that STL algorithms which mutate the order of
// iterators, such as reverse() and unique(), will not work by default with
// SpdyIntrusiveLists. In order to allow these algorithms to work you'll need to
// define swap() and/or operator= for your class.
//
// Another limitation is that the SpdyIntrusiveList<> structure itself is not
// copyable or assignable since an item/link combination can only exist on one
// SpdyIntrusiveList<> at a time. This limitation is a result of the link
// pointers for an item being intrusive in the item itself. For example, the
// following will not compile:
//
//   FooList a;
//   FooList b(a); // no copy constructor
//   b = a;        // no assignment operator
//
// The similar STL code does work since the link pointers are external to the
// item:
//
//   list<int*> a;
//   a.push_back(new int);
//   list<int*> b(a);
//   CHECK(a.front() == b.front());
//
// Note that SpdyIntrusiveList::size() runs in O(N) time.

#include <stddef.h>
#include <iterator>


namespace spdy {

template <typename T, typename ListID> class SpdyIntrusiveList;

template <typename T, typename ListID = void> class SpdyIntrusiveLink {
 protected:
  // We declare the constructor protected so that only derived types and the
  // befriended list can construct this.
  SpdyIntrusiveLink() : next_(nullptr), prev_(nullptr) {}

#ifndef SWIG
  SpdyIntrusiveLink(const SpdyIntrusiveLink&) = delete;
  SpdyIntrusiveLink& operator=(const SpdyIntrusiveLink&) = delete;
#endif  // SWIG

 private:
  // We befriend the matching list type so that it can manipulate the links
  // while they are kept private from others.
  friend class SpdyIntrusiveList<T, ListID>;

  // Encapsulates the logic to convert from a link to its derived type.
  T* cast_to_derived() { return static_cast<T*>(this); }
  const T* cast_to_derived() const { return static_cast<const T*>(this); }

  SpdyIntrusiveLink* next_;
  SpdyIntrusiveLink* prev_;
};

template <typename T, typename ListID = void> class SpdyIntrusiveList {
  template <typename QualifiedT, typename QualifiedLinkT> class iterator_impl;

 public:
  typedef T value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;

  typedef SpdyIntrusiveLink<T, ListID> link_type;
  typedef iterator_impl<T, link_type> iterator;
  typedef iterator_impl<const T, const link_type> const_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;

  SpdyIntrusiveList() { clear(); }
  // After the move constructor the moved-from list will be empty.
  //
  // NOTE: There is no move assign operator (for now).
  // The reason is that at the moment 'clear()' does not unlink the nodes.
  // It makes is_linked() return true when it should return false.
  // If such node is removed from the list (e.g. from its destructor), or is
  // added to another list - a memory corruption will occur.
  // Admitedly the destructor does not unlink the nodes either, but move-assign
  // will likely make the problem more prominent.
#ifndef SWIG
  SpdyIntrusiveList(SpdyIntrusiveList&& src) noexcept {
    clear();
    if (src.empty()) return;
    sentinel_link_.next_ = src.sentinel_link_.next_;
    sentinel_link_.prev_ = src.sentinel_link_.prev_;
    // Fix head and tail nodes of the list.
    sentinel_link_.prev_->next_ = &sentinel_link_;
    sentinel_link_.next_->prev_ = &sentinel_link_;
    src.clear();
  }
#endif  // SWIG

  iterator begin() { return iterator(sentinel_link_.next_); }
  const_iterator begin() const { return const_iterator(sentinel_link_.next_); }
  iterator end() { return iterator(&sentinel_link_); }
  const_iterator end() const { return const_iterator(&sentinel_link_); }
  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  bool empty() const { return (sentinel_link_.next_ == &sentinel_link_); }
  // This runs in O(N) time.
  size_type size() const { return std::distance(begin(), end()); }
  size_type max_size() const { return size_type(-1); }

  reference front() { return *begin(); }
  const_reference front() const { return *begin(); }
  reference back() { return *(--end()); }
  const_reference back() const { return *(--end()); }

  static iterator insert(iterator position, T *obj) {
    return insert_link(position.link(), obj);
  }
  void push_front(T* obj) { insert(begin(), obj); }
  void push_back(T* obj) { insert(end(), obj); }

  static iterator erase(T* obj) {
    link_type* obj_link = obj;
    // Fix up the next and previous links for the previous and next objects.
    obj_link->next_->prev_ = obj_link->prev_;
    obj_link->prev_->next_ = obj_link->next_;
    // Zero out the next and previous links for the removed item. This will
    // cause any future attempt to remove the item from the list to cause a
    // crash instead of possibly corrupting the list structure.
    link_type* next_link = obj_link->next_;
    obj_link->next_ = nullptr;
    obj_link->prev_ = nullptr;
    return iterator(next_link);
  }

  static iterator erase(iterator position) {
    return erase(position.operator->());
  }
  void pop_front() { erase(begin()); }
  void pop_back() { erase(--end()); }

  // Check whether the given element is linked into some list. Note that this
  // does *not* check whether it is linked into a particular list.
  // Also, if clear() is used to clear the containing list, is_linked() will
  // still return true even though obj is no longer in any list.
  static bool is_linked(const T* obj) {
    return obj->link_type::next_ != nullptr;
  }

  void clear() {
    sentinel_link_.next_ = sentinel_link_.prev_ = &sentinel_link_;
  }
  void swap(SpdyIntrusiveList& x) {
    SpdyIntrusiveList tmp;
    tmp.splice(tmp.begin(), *this);
    this->splice(this->begin(), x);
    x.splice(x.begin(), tmp);
  }

  void splice(iterator pos, SpdyIntrusiveList& src) {
    splice(pos, src.begin(), src.end());
  }

  void splice(iterator pos, iterator i) { splice(pos, i, std::next(i)); }

  void splice(iterator pos, iterator first, iterator last) {
    if (first == last) return;

    link_type* const last_prev = last.link()->prev_;

    // Remove from the source.
    first.link()->prev_->next_ = last.operator->();
    last.link()->prev_ = first.link()->prev_;

    // Attach to the destination.
    first.link()->prev_ = pos.link()->prev_;
    pos.link()->prev_->next_ = first.operator->();
    last_prev->next_ = pos.operator->();
    pos.link()->prev_ = last_prev;
  }

 private:
  static iterator insert_link(link_type* next_link, T* obj) {
    link_type* obj_link = obj;
    obj_link->next_ = next_link;
    link_type* const initial_next_prev = next_link->prev_;
    obj_link->prev_ = initial_next_prev;
    initial_next_prev->next_ = obj_link;
    next_link->prev_ = obj_link;
    return iterator(obj_link);
  }

  // The iterator implementation is parameterized on a potentially qualified
  // variant of T and the matching qualified link type. Essentially, QualifiedT
  // will either be 'T' or 'const T', the latter for a const_iterator.
  template <typename QualifiedT, typename QualifiedLinkT>
  class iterator_impl : public std::iterator<std::bidirectional_iterator_tag,
                                             QualifiedT> {
   public:
    typedef std::iterator<std::bidirectional_iterator_tag, QualifiedT> base;

    iterator_impl() : link_(nullptr) {}
    iterator_impl(QualifiedLinkT* link) : link_(link) {}
    iterator_impl(const iterator_impl& x) : link_(x.link_) {}

    // Allow converting and comparing across iterators where the pointer
    // assignment and comparisons (respectively) are allowed.
    template <typename U, typename V>
    iterator_impl(const iterator_impl<U, V>& x) : link_(x.link_) {}
    template <typename U, typename V>
    bool operator==(const iterator_impl<U, V>& x) const {
      return link_ == x.link_;
    }
    template <typename U, typename V>
    bool operator!=(const iterator_impl<U, V>& x) const {
      return link_ != x.link_;
    }

    typename base::reference operator*() const { return *operator->(); }
    typename base::pointer operator->() const {
      return link_->cast_to_derived();
    }

    QualifiedLinkT *link() const { return link_; }

#ifndef SWIG  // SWIG can't wrap these operator overloads.
    iterator_impl& operator++() { link_ = link_->next_; return *this; }
    iterator_impl operator++(int /*unused*/) {
      iterator_impl tmp = *this;
      ++*this;
      return tmp;
    }
    iterator_impl& operator--() { link_ = link_->prev_; return *this; }
    iterator_impl operator--(int /*unused*/) {
      iterator_impl tmp = *this;
      --*this;
      return tmp;
    }
#endif  // SWIG

   private:
    // Ensure iterators can access other iterators node directly.
    template <typename U, typename V> friend class iterator_impl;

    QualifiedLinkT* link_;
  };

  // This bare link acts as the sentinel node.
  link_type sentinel_link_;

  // These are private and undefined to prevent copying and assigning.
  SpdyIntrusiveList(const SpdyIntrusiveList&);
  void operator=(const SpdyIntrusiveList&);
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_INTRUSIVE_LIST_H_
