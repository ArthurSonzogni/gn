// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_toolchain_writer.h"

#include "gn/string_output_buffer.h"
#include "gn/test_with_scope.h"
#include "util/test/test.h"

TEST(NinjaToolchainWriter, WriteToolRule) {
  TestWithScope setup;

  StringOutputBuffer stream;
  NinjaToolchainWriter writer(setup.settings(), setup.toolchain(), stream);
  writer.WriteToolRule(setup.toolchain()->GetTool(CTool::kCToolCc),
                       std::string("prefix_"));

  EXPECT_EQ(
      "rule prefix_cc\n"
      "  command = cc ${in} ${cflags} ${cflags_c} ${defines} ${include_dirs} "
      "-o ${out}\n",
      stream);
}

TEST(NinjaToolchainWriter, WriteToolRuleWithLauncher) {
  TestWithScope setup;

  StringOutputBuffer stream;
  NinjaToolchainWriter writer(setup.settings(), setup.toolchain(), stream);
  writer.WriteToolRule(setup.toolchain()->GetTool(CTool::kCToolCxx),
                       std::string("prefix_"));

  EXPECT_EQ(
      "rule prefix_cxx\n"
      "  command = launcher c++ ${in} ${cflags} ${cflags_cc} ${defines} "
      "${include_dirs} ${module_deps} "
      "-o ${out}\n",
      stream);
}

TEST(NinjaToolchainWriter, WriteToolRuleWithInputsPhony) {
  TestWithScope setup;

  std::unique_ptr<Tool> tool = Tool::CreateTool(CTool::kCToolCxx);
  TestWithScope::SetCommandForTool(
      "launcher c++ {{source}} {{cflags}} {{cflags_cc}} {{defines}} "
      "{{include_dirs}} -o {{output}}",
      tool.get());
  tool->set_inputs(
      {SourceFile("//bin/clang++"), SourceFile("//bin/plugin.so")});

  StringOutputBuffer stream;
  NinjaToolchainWriter writer(setup.settings(), setup.toolchain(), stream);
  writer.WriteToolRule(tool.get(), std::string("prefix_"));

  EXPECT_EQ(
      "rule prefix_cxx\n"
      "  command = launcher c++ ${in} ${cflags} ${cflags_cc} ${defines} "
      "${include_dirs} "
      "-o ${out}\n"
      "build phony/prefix_cxx_inputs: phony ../../bin/clang++ "
      "../../bin/plugin.so\n",
      stream);
}
