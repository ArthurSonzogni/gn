// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FFI_TEST_WITH_SCOPE_H_
#define TOOLS_GN_FFI_TEST_WITH_SCOPE_H_

#include <memory>

#include "base/command_line.h"
#include "gn/scheduler.h"
#include "gn/test_with_scope.h"

struct TestWithScopeAndScheduler : public TestWithScope {
  Scheduler scheduler;
};

inline std::unique_ptr<TestWithScope> NewTestWithScope() {
  if (!base::CommandLine::InitializedForCurrentProcess()) {
    int argc = 1;
    const char* argv[] = {"gn_rust_tests", nullptr};
    base::CommandLine::Init(argc, argv);
  }
  return std::make_unique<TestWithScopeAndScheduler>();
}

#endif  // TOOLS_GN_FFI_TEST_WITH_SCOPE_H_
