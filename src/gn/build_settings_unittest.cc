// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/build_settings.h"

#include "base/files/file_path.h"
#include "gn/source_dir.h"
#include "util/build_config.h"
#include "util/test/test.h"

TEST(BuildSettingsTest, PythonPathWithBuildDirOutsideSourceRoot) {
  BuildSettings settings;
#if defined(OS_WIN)
  settings.SetRootPath(base::FilePath(FILE_PATH_LITERAL("C:/src")));
  settings.SetBuildDir(SourceDir("/C:/out/"));
#else
  settings.SetRootPath(base::FilePath(FILE_PATH_LITERAL("/src")));
  settings.SetBuildDir(SourceDir("/out/"));
#endif
  settings.SetPythonPath(settings.root_path()
                             .Append(FILE_PATH_LITERAL("bin/python3"))
                             .NormalizePathSeparatorsTo('/'));

  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("../src/bin/python3")),
            settings.python_path());
  EXPECT_TRUE(settings.python_path_is_relative_to_build_dir());
}

#if defined(OS_WIN)
TEST(BuildSettingsTest, PythonPathWithBuildDirOnDifferentDrive) {
  BuildSettings settings;
  settings.SetRootPath(base::FilePath(FILE_PATH_LITERAL("D:/src")));
  settings.SetBuildDir(SourceDir("/C:/out/"));
  const base::FilePath python_path(FILE_PATH_LITERAL("D:/src/bin/python3.exe"));
  settings.SetPythonPath(python_path);

  EXPECT_EQ(python_path, settings.python_path());
  EXPECT_FALSE(settings.python_path_is_relative_to_build_dir());
}
#endif  // defined(OS_WIN)
