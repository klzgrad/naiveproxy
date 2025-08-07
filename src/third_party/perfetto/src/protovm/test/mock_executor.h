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

#ifndef SRC_PROTOVM_TEST_MOCK_EXECUTOR_H_
#define SRC_PROTOVM_TEST_MOCK_EXECUTOR_H_

#include <cstdint>

#include "test/gtest_and_gmock.h"

#include "src/protovm/executor.h"
#include "src/protovm/rw_proto.h"

namespace perfetto {
namespace protovm {
namespace test {

class MockExecutor : public Executor {
 public:
  MOCK_CONST_METHOD2(EnterField,
                     StatusOr<void>(Cursors* cursors, uint32_t field_id));
  MOCK_CONST_METHOD3(EnterRepeatedFieldAt,
                     StatusOr<void>(Cursors* cursors,
                                    uint32_t field_id,
                                    uint32_t index));
  MOCK_CONST_METHOD4(EnterRepeatedFieldByKey,
                     StatusOr<void>(Cursors* cursors,
                                    uint32_t field_id,
                                    uint32_t map_key_field_id,
                                    uint64_t key));
  MOCK_CONST_METHOD2(
      IterateRepeatedField,
      StatusOr<RoCursor::RepeatedFieldIterator>(RoCursor* src,
                                                uint32_t field_id));
  MOCK_CONST_METHOD2(
      IterateRepeatedField,
      StatusOr<RwProto::Cursor::RepeatedFieldIterator>(RwProto::Cursor* src,
                                                       uint32_t field_id));
  MOCK_CONST_METHOD1(ReadRegister, StatusOr<uint64_t>(uint8_t reg_id));
  MOCK_METHOD2(WriteRegister,
               StatusOr<void>(const Cursors& cursors, uint8_t reg_id));
  MOCK_CONST_METHOD1(Delete, StatusOr<void>(RwProto::Cursor* dst));
  MOCK_CONST_METHOD1(Merge, StatusOr<void>(Cursors* cursors));
  MOCK_CONST_METHOD1(Set, StatusOr<void>(Cursors* cursors));
};

}  // namespace test
}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_TEST_MOCK_EXECUTOR_H_
