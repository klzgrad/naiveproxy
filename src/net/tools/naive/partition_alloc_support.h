// Copyright 2021 The Chromium Authors
// Copyright 2022 klzgrad <kizdiv@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_PARTITION_ALLOC_SUPPORT_H_
#define NET_TOOLS_NAIVE_PARTITION_ALLOC_SUPPORT_H_

namespace naive_partition_alloc_support {

void ReconfigureEarly();
void ReconfigureAfterFeatureListInit();
void ReconfigureAfterTaskRunnerInit();

}  // namespace naive_partition_alloc_support

#endif  // NET_TOOLS_NAIVE_PARTITION_ALLOC_SUPPORT_H_
