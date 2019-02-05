// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted_memory.h"

#include <stdint.h>

#include <utility>

#include "base/memory/read_only_shared_memory_region.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::Each;
using testing::ElementsAre;

namespace base {

TEST(RefCountedMemoryUnitTest, RefCountedStaticMemory) {
  auto mem = MakeRefCounted<RefCountedStaticMemory>("static mem00", 10);

  EXPECT_EQ(10U, mem->size());
  EXPECT_EQ("static mem", std::string(mem->front_as<char>(), mem->size()));
}

TEST(RefCountedMemoryUnitTest, RefCountedBytes) {
  std::vector<uint8_t> data;
  data.push_back(45);
  data.push_back(99);
  scoped_refptr<RefCountedMemory> mem = RefCountedBytes::TakeVector(&data);

  EXPECT_EQ(0U, data.size());

  ASSERT_EQ(2U, mem->size());
  EXPECT_EQ(45U, mem->front()[0]);
  EXPECT_EQ(99U, mem->front()[1]);

  scoped_refptr<RefCountedMemory> mem2;
  {
    const unsigned char kData[] = {12, 11, 99};
    mem2 = MakeRefCounted<RefCountedBytes>(kData, arraysize(kData));
  }
  ASSERT_EQ(3U, mem2->size());
  EXPECT_EQ(12U, mem2->front()[0]);
  EXPECT_EQ(11U, mem2->front()[1]);
  EXPECT_EQ(99U, mem2->front()[2]);
}

TEST(RefCountedMemoryUnitTest, RefCountedBytesMutable) {
  auto mem = base::MakeRefCounted<RefCountedBytes>(10);

  ASSERT_EQ(10U, mem->size());
  EXPECT_THAT(mem->data(), Each(0U));

  // Test non-const versions of data(), front() and front_as<>().
  mem->data()[0] = 1;
  mem->front()[1] = 2;
  mem->front_as<char>()[2] = 3;

  EXPECT_THAT(mem->data(), ElementsAre(1, 2, 3, 0, 0, 0, 0, 0, 0, 0));
}

TEST(RefCountedMemoryUnitTest, RefCountedString) {
  std::string s("destroy me");
  scoped_refptr<RefCountedMemory> mem = RefCountedString::TakeString(&s);

  EXPECT_EQ(0U, s.size());

  ASSERT_EQ(10U, mem->size());
  EXPECT_EQ('d', mem->front()[0]);
  EXPECT_EQ('e', mem->front()[1]);
  EXPECT_EQ('e', mem->front()[9]);
}

TEST(RefCountedMemoryUnitTest, RefCountedSharedMemory) {
  static const char kData[] = "shm_dummy_data";
  auto shm = std::make_unique<SharedMemory>();
  ASSERT_TRUE(shm->CreateAndMapAnonymous(sizeof(kData)));
  memcpy(shm->memory(), kData, sizeof(kData));

  auto mem =
      MakeRefCounted<RefCountedSharedMemory>(std::move(shm), sizeof(kData));
  ASSERT_EQ(sizeof(kData), mem->size());
  EXPECT_EQ('s', mem->front()[0]);
  EXPECT_EQ('h', mem->front()[1]);
  EXPECT_EQ('_', mem->front()[9]);
}

TEST(RefCountedMemoryUnitTest, RefCountedSharedMemoryMapping) {
  static const char kData[] = "mem_region_dummy_data";
  scoped_refptr<RefCountedSharedMemoryMapping> mem;
  {
    MappedReadOnlyRegion region =
        ReadOnlySharedMemoryRegion::Create(sizeof(kData));
    ReadOnlySharedMemoryMapping ro_mapping = region.region.Map();
    WritableSharedMemoryMapping rw_mapping = std::move(region.mapping);
    ASSERT_TRUE(rw_mapping.IsValid());
    memcpy(rw_mapping.memory(), kData, sizeof(kData));
    mem = MakeRefCounted<RefCountedSharedMemoryMapping>(std::move(ro_mapping));
  }

  ASSERT_LE(sizeof(kData), mem->size());
  EXPECT_EQ('e', mem->front()[1]);
  EXPECT_EQ('m', mem->front()[2]);
  EXPECT_EQ('o', mem->front()[8]);

  {
    MappedReadOnlyRegion region =
        ReadOnlySharedMemoryRegion::Create(sizeof(kData));
    WritableSharedMemoryMapping rw_mapping = std::move(region.mapping);
    ASSERT_TRUE(rw_mapping.IsValid());
    memcpy(rw_mapping.memory(), kData, sizeof(kData));
    mem = RefCountedSharedMemoryMapping::CreateFromWholeRegion(region.region);
  }

  ASSERT_LE(sizeof(kData), mem->size());
  EXPECT_EQ('_', mem->front()[3]);
  EXPECT_EQ('r', mem->front()[4]);
  EXPECT_EQ('i', mem->front()[7]);
}

TEST(RefCountedMemoryUnitTest, Equals) {
  std::string s1("same");
  scoped_refptr<RefCountedMemory> mem1 = RefCountedString::TakeString(&s1);

  std::vector<unsigned char> d2 = {'s', 'a', 'm', 'e'};
  scoped_refptr<RefCountedMemory> mem2 = RefCountedBytes::TakeVector(&d2);

  EXPECT_TRUE(mem1->Equals(mem2));

  std::string s3("diff");
  scoped_refptr<RefCountedMemory> mem3 = RefCountedString::TakeString(&s3);

  EXPECT_FALSE(mem1->Equals(mem3));
  EXPECT_FALSE(mem2->Equals(mem3));
}

TEST(RefCountedMemoryUnitTest, EqualsNull) {
  std::string s("str");
  scoped_refptr<RefCountedMemory> mem = RefCountedString::TakeString(&s);
  EXPECT_FALSE(mem->Equals(nullptr));
}

}  //  namespace base
