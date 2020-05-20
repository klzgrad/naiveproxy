// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_intrusive_list.h"

#include <algorithm>
#include <cstddef>
#include <list>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

namespace spdy {
namespace test {

struct ListId2 {};

struct TestItem : public SpdyIntrusiveLink<TestItem>,
                  public SpdyIntrusiveLink<TestItem, ListId2> {
  int n;
};
typedef SpdyIntrusiveList<TestItem> TestList;
typedef std::list<TestItem*> CanonicalList;

void swap(TestItem &a, TestItem &b) {
  using std::swap;
  swap(a.n, b.n);
}

class IntrusiveListTest : public QuicheTest {
 protected:
  void CheckLists() {
    CheckLists(l1, ll1);
    if (QuicheTest::HasFailure())
      return;
    CheckLists(l2, ll2);
  }

  void CheckLists(const TestList &list_a, const CanonicalList &list_b) {
    ASSERT_EQ(list_a.size(), list_b.size());
    TestList::const_iterator it_a = list_a.begin();
    CanonicalList::const_iterator it_b = list_b.begin();
    while (it_a != list_a.end()) {
      EXPECT_EQ(&*it_a++, *it_b++);
    }
    EXPECT_EQ(list_a.end(), it_a);
    EXPECT_EQ(list_b.end(), it_b);
  }

  void PrepareLists(int num_elems_1, int num_elems_2 = 0) {
    FillLists(&l1, &ll1, e, num_elems_1);
    FillLists(&l2, &ll2, e + num_elems_1, num_elems_2);
  }

  void FillLists(TestList *list_a, CanonicalList *list_b, TestItem *elems,
                 int num_elems) {
    list_a->clear();
    list_b->clear();
    for (int i = 0; i < num_elems; ++i) {
      list_a->push_back(elems + i);
      list_b->push_back(elems + i);
    }
    CheckLists(*list_a, *list_b);
  }

  TestItem e[10];
  TestList l1, l2;
  CanonicalList ll1, ll2;
};

TEST(NewIntrusiveListTest, Basic) {
  TestList list1;

  EXPECT_EQ(sizeof(SpdyIntrusiveLink<TestItem>), sizeof(void*) * 2);

  for (int i = 0; i < 10; ++i) {
    TestItem *e = new TestItem;
    e->n = i;
    list1.push_front(e);
  }
  EXPECT_EQ(list1.size(), 10u);

  // Verify we can reverse a list because we defined swap for TestItem.
  std::reverse(list1.begin(), list1.end());
  EXPECT_EQ(list1.size(), 10u);

  // Check both const and non-const forward iteration.
  const TestList& clist1 = list1;
  int i = 0;
  TestList::iterator iter = list1.begin();
  for (;
       iter != list1.end();
       ++iter, ++i) {
    EXPECT_EQ(iter->n, i);
  }
  EXPECT_EQ(iter, clist1.end());
  EXPECT_NE(iter, clist1.begin());
  i = 0;
  iter = list1.begin();
  for (;
       iter != list1.end();
       ++iter, ++i) {
    EXPECT_EQ(iter->n, i);
  }
  EXPECT_EQ(iter, clist1.end());
  EXPECT_NE(iter, clist1.begin());

  EXPECT_EQ(list1.front().n, 0);
  EXPECT_EQ(list1.back().n, 9);

  // Verify we can swap 2 lists.
  TestList list2;
  list2.swap(list1);
  EXPECT_EQ(list1.size(), 0u);
  EXPECT_EQ(list2.size(), 10u);

  // Check both const and non-const reverse iteration.
  const TestList& clist2 = list2;
  TestList::reverse_iterator riter = list2.rbegin();
  i = 9;
  for (;
       riter != list2.rend();
       ++riter, --i) {
    EXPECT_EQ(riter->n, i);
  }
  EXPECT_EQ(riter, clist2.rend());
  EXPECT_NE(riter, clist2.rbegin());

  riter = list2.rbegin();
  i = 9;
  for (;
       riter != list2.rend();
       ++riter, --i) {
    EXPECT_EQ(riter->n, i);
  }
  EXPECT_EQ(riter, clist2.rend());
  EXPECT_NE(riter, clist2.rbegin());

  while (!list2.empty()) {
    TestItem *e = &list2.front();
    list2.pop_front();
    delete e;
  }
}

TEST(NewIntrusiveListTest, Erase) {
  TestList l;
  TestItem *e[10];

  // Create a list with 10 items.
  for (int i = 0; i < 10; ++i) {
    e[i] = new TestItem;
    l.push_front(e[i]);
  }

  // Test that erase works.
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(l.size(), (10u - i));

    TestList::iterator iter = l.erase(e[i]);
    EXPECT_NE(iter, TestList::iterator(e[i]));

    EXPECT_EQ(l.size(), (10u - i - 1));
    delete e[i];
  }
}

TEST(NewIntrusiveListTest, Insert) {
  TestList l;
  TestList::iterator iter = l.end();
  TestItem *e[10];

  // Create a list with 10 items.
  for (int i = 9; i >= 0; --i) {
    e[i] = new TestItem;
    iter = l.insert(iter, e[i]);
    EXPECT_EQ(&(*iter), e[i]);
  }

  EXPECT_EQ(l.size(), 10u);

  // Verify insertion order.
  iter = l.begin();
  for (TestItem *item : e) {
    EXPECT_EQ(&(*iter), item);
    iter = l.erase(item);
    delete item;
  }
}

TEST(NewIntrusiveListTest, Move) {
  // Move contructible.

  {  // Move-construct from an empty list.
    TestList src;
    TestList dest(std::move(src));
    EXPECT_TRUE(dest.empty());
  }

  {  // Move-construct from a single item list.
    TestItem e;
    TestList src;
    src.push_front(&e);

    TestList dest(std::move(src));
    EXPECT_TRUE(src.empty());  // NOLINT bugprone-use-after-move
    ASSERT_THAT(dest.size(), 1);
    EXPECT_THAT(&dest.front(), &e);
    EXPECT_THAT(&dest.back(), &e);
  }

  {  // Move-construct from a list with multiple items.
    TestItem items[10];
    TestList src;
    for (TestItem &e : items) src.push_back(&e);

    TestList dest(std::move(src));
    EXPECT_TRUE(src.empty());  // NOLINT bugprone-use-after-move
    // Verify the items on the destination list.
    ASSERT_THAT(dest.size(), 10);
    int i = 0;
    for (TestItem &e : dest) {
      EXPECT_THAT(&e, &items[i++]) << " for index " << i;
    }
  }
}

TEST(NewIntrusiveListTest, StaticInsertErase) {
  TestList l;
  TestItem e[2];
  TestList::iterator i = l.begin();
  TestList::insert(i, &e[0]);
  TestList::insert(&e[0], &e[1]);
  TestList::erase(&e[0]);
  TestList::erase(TestList::iterator(&e[1]));
  EXPECT_TRUE(l.empty());
}

TEST_F(IntrusiveListTest, Splice) {
  // We verify that the contents of this secondary list aren't affected by any
  // of the splices.
  SpdyIntrusiveList<TestItem, ListId2> secondary_list;
  for (int i  = 0; i < 3; ++i) {
    secondary_list.push_back(&e[i]);
  }

  // Test the basic cases:
  // - The lists range from 0 to 2 elements.
  // - The insertion point ranges from begin() to end()
  // - The transfered range has multiple sizes and locations in the source.
  for (int l1_count = 0; l1_count < 3; ++l1_count) {
    for (int l2_count = 0; l2_count < 3; ++l2_count) {
      for (int pos = 0; pos <= l1_count; ++pos) {
        for (int first = 0; first <= l2_count; ++first) {
          for (int last = first; last <= l2_count; ++last) {
            PrepareLists(l1_count, l2_count);

            l1.splice(std::next(l1.begin(), pos), std::next(l2.begin(), first),
                      std::next(l2.begin(), last));
            ll1.splice(std::next(ll1.begin(), pos), ll2,
                       std::next(ll2.begin(), first),
                       std::next(ll2.begin(), last));

            CheckLists();

            ASSERT_EQ(3u, secondary_list.size());
            for (int i = 0; i < 3; ++i) {
              EXPECT_EQ(&e[i], &*std::next(secondary_list.begin(), i));
            }
          }
        }
      }
    }
  }
}

// Build up a set of classes which form "challenging" type hierarchies to use
// with an SpdyIntrusiveList.
struct BaseLinkId {};
struct DerivedLinkId {};

struct AbstractBase : public SpdyIntrusiveLink<AbstractBase, BaseLinkId> {
  virtual ~AbstractBase() = 0;
  virtual std::string name() { return "AbstractBase"; }
};
AbstractBase::~AbstractBase() {}
struct DerivedClass : public SpdyIntrusiveLink<DerivedClass, DerivedLinkId>,
                      public AbstractBase {
  ~DerivedClass() override {}
  std::string name() override { return "DerivedClass"; }
};
struct VirtuallyDerivedBaseClass : public virtual AbstractBase {
  ~VirtuallyDerivedBaseClass() override = 0;
  std::string name() override { return "VirtuallyDerivedBaseClass"; }
};
VirtuallyDerivedBaseClass::~VirtuallyDerivedBaseClass() {}
struct VirtuallyDerivedClassA
    : public SpdyIntrusiveLink<VirtuallyDerivedClassA, DerivedLinkId>,
      public virtual VirtuallyDerivedBaseClass {
  ~VirtuallyDerivedClassA() override {}
  std::string name() override { return "VirtuallyDerivedClassA"; }
};
struct NonceClass {
  virtual ~NonceClass() {}
  int data_;
};
struct VirtuallyDerivedClassB
    : public SpdyIntrusiveLink<VirtuallyDerivedClassB, DerivedLinkId>,
      public virtual NonceClass,
      public virtual VirtuallyDerivedBaseClass {
  ~VirtuallyDerivedClassB() override {}
  std::string name() override { return "VirtuallyDerivedClassB"; }
};
struct VirtuallyDerivedClassC
    : public SpdyIntrusiveLink<VirtuallyDerivedClassC, DerivedLinkId>,
      public virtual AbstractBase,
      public virtual NonceClass,
      public virtual VirtuallyDerivedBaseClass {
  ~VirtuallyDerivedClassC() override {}
  std::string name() override { return "VirtuallyDerivedClassC"; }
};

// Test for multiple layers between the element type and the link.
namespace templated_base_link {
template <typename T> struct AbstractBase : public SpdyIntrusiveLink<T> {
  virtual ~AbstractBase() = 0;
};
template <typename T> AbstractBase<T>::~AbstractBase() {}
struct DerivedClass : public AbstractBase<DerivedClass> {
  int n;
};
}

TEST(NewIntrusiveListTest, HandleInheritanceHierarchies) {
  {
    SpdyIntrusiveList<DerivedClass, DerivedLinkId> list;
    DerivedClass elements[2];
    EXPECT_TRUE(list.empty());
    list.push_back(&elements[0]);
    EXPECT_EQ(1u, list.size());
    list.push_back(&elements[1]);
    EXPECT_EQ(2u, list.size());
    list.pop_back();
    EXPECT_EQ(1u, list.size());
    list.pop_back();
    EXPECT_TRUE(list.empty());
  }
  {
    SpdyIntrusiveList<VirtuallyDerivedClassA, DerivedLinkId> list;
    VirtuallyDerivedClassA elements[2];
    EXPECT_TRUE(list.empty());
    list.push_back(&elements[0]);
    EXPECT_EQ(1u, list.size());
    list.push_back(&elements[1]);
    EXPECT_EQ(2u, list.size());
    list.pop_back();
    EXPECT_EQ(1u, list.size());
    list.pop_back();
    EXPECT_TRUE(list.empty());
  }
  {
    SpdyIntrusiveList<VirtuallyDerivedClassC, DerivedLinkId> list;
    VirtuallyDerivedClassC elements[2];
    EXPECT_TRUE(list.empty());
    list.push_back(&elements[0]);
    EXPECT_EQ(1u, list.size());
    list.push_back(&elements[1]);
    EXPECT_EQ(2u, list.size());
    list.pop_back();
    EXPECT_EQ(1u, list.size());
    list.pop_back();
    EXPECT_TRUE(list.empty());
  }
  {
    SpdyIntrusiveList<AbstractBase, BaseLinkId> list;
    DerivedClass d1;
    VirtuallyDerivedClassA d2;
    VirtuallyDerivedClassB d3;
    VirtuallyDerivedClassC d4;
    EXPECT_TRUE(list.empty());
    list.push_back(&d1);
    EXPECT_EQ(1u, list.size());
    list.push_back(&d2);
    EXPECT_EQ(2u, list.size());
    list.push_back(&d3);
    EXPECT_EQ(3u, list.size());
    list.push_back(&d4);
    EXPECT_EQ(4u, list.size());
    SpdyIntrusiveList<AbstractBase, BaseLinkId>::iterator it = list.begin();
    EXPECT_EQ("DerivedClass",           (it++)->name());
    EXPECT_EQ("VirtuallyDerivedClassA", (it++)->name());
    EXPECT_EQ("VirtuallyDerivedClassB", (it++)->name());
    EXPECT_EQ("VirtuallyDerivedClassC", (it++)->name());
  }
  {
    SpdyIntrusiveList<templated_base_link::DerivedClass> list;
    templated_base_link::DerivedClass elements[2];
    EXPECT_TRUE(list.empty());
    list.push_back(&elements[0]);
    EXPECT_EQ(1u, list.size());
    list.push_back(&elements[1]);
    EXPECT_EQ(2u, list.size());
    list.pop_back();
    EXPECT_EQ(1u, list.size());
    list.pop_back();
    EXPECT_TRUE(list.empty());
  }
}


class IntrusiveListTagTypeTest : public testing::Test {
 protected:
  struct Tag {};
  class Element : public SpdyIntrusiveLink<Element, Tag> {};
};

TEST_F(IntrusiveListTagTypeTest, TagTypeListID) {
  SpdyIntrusiveList<Element, Tag> list;
  {
    Element e;
    list.push_back(&e);
  }
}

}  // namespace test
}  // namespace spdy
