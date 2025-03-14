// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/file_writer.h"

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "gn/filesystem_utils.h"

#include "util/build_config.h"
#include "util/sys_info.h"
#include "util/test/test.h"

TEST(FileWriter, SingleWrite) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const std::string data = "foo";

  base::FilePath file_path = temp_dir.GetPath().AppendASCII("foo.txt");

  FileWriter writer;
  EXPECT_TRUE(writer.Create(file_path));
  EXPECT_TRUE(writer.Write(data));
  EXPECT_TRUE(writer.Close());

  EXPECT_TRUE(ContentsEqual(file_path, data));
}

TEST(FileWriter, MultipleWrites) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const std::string data = "Hello World!";

  base::FilePath file_path = temp_dir.GetPath().AppendASCII("foo.txt");

  FileWriter writer;
  EXPECT_TRUE(writer.Create(file_path));
  EXPECT_TRUE(writer.Write("Hello "));
  EXPECT_TRUE(writer.Write("World!"));
  EXPECT_TRUE(writer.Close());

  EXPECT_TRUE(ContentsEqual(file_path, data));
}

#if defined(OS_WIN)
TEST(FileWriter, LongPathWrite) {
  if (!IsLongPathsSupportEnabled())
    return;

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const std::string data = "Hello World!";

  base::FilePath file_path =
      temp_dir.GetPath().AppendASCII(std::string(255, 'A'));

  EXPECT_GE(file_path.value().size(), MAX_PATH);

  FileWriter writer;
  EXPECT_TRUE(writer.Create(file_path));
  EXPECT_TRUE(writer.Write(data));
  EXPECT_TRUE(writer.Close());

  EXPECT_TRUE(ContentsEqual(file_path, data));
}
#endif