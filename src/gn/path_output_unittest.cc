// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/path_output.h"

#include "base/files/file_path.h"
#include "gn/output_file.h"
#include "gn/source_dir.h"
#include "gn/source_file.h"
#include "gn/string_output_buffer.h"
#include "util/build_config.h"
#include "util/test/test.h"

TEST(PathOutput, Basic) {
  SourceDir build_dir("//out/Debug/");
  std::string_view source_root("/source/root");
  PathOutput writer(build_dir, source_root, ESCAPE_NONE);
  {
    // Normal source-root path.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/bar.cc"));
    EXPECT_EQ("../../foo/bar.cc", out);
  }
  {
    // File in the root dir.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo.cc"));
    EXPECT_EQ("../../foo.cc", out);
  }
  {
    // Files in the output dir.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//out/Debug/foo.cc"));
    out << " ";
    writer.WriteFile(out, SourceFile("//out/Debug/bar/baz.cc"));
    EXPECT_EQ("foo.cc bar/baz.cc", out);
  }
#if defined(OS_WIN)
  {
    // System-absolute path.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("/C:/foo/bar.cc"));
    EXPECT_EQ("C:/foo/bar.cc", out);
  }
#else
  {
    // System-absolute path.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("/foo/bar.cc"));
    EXPECT_EQ("/foo/bar.cc", out);
  }
#endif
}

// Same as basic but the output dir is the root.
TEST(PathOutput, BasicInRoot) {
  SourceDir build_dir("//");
  std::string_view source_root("/source/root");
  PathOutput writer(build_dir, source_root, ESCAPE_NONE);
  {
    // Normal source-root path.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/bar.cc"));
    EXPECT_EQ("foo/bar.cc", out);
  }
  {
    // File in the root dir.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo.cc"));
    EXPECT_EQ("foo.cc", out);
  }
}

TEST(PathOutput, NinjaEscaping) {
  SourceDir build_dir("//out/Debug/");
  std::string_view source_root("/source/root");
  PathOutput writer(build_dir, source_root, ESCAPE_NINJA);
  {
    // Spaces and $ in filenames.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/foo bar$.cc"));
    EXPECT_EQ("../../foo/foo$ bar$$.cc", out);
  }
  {
    // Not other weird stuff
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/\"foo\".cc"));
    EXPECT_EQ("../../foo/\"foo\".cc", out);
  }
}

TEST(PathOutput, NinjaForkEscaping) {
  SourceDir build_dir("//out/Debug/");
  std::string_view source_root("/source/root");
  PathOutput writer(build_dir, source_root, ESCAPE_NINJA_COMMAND);

  // Spaces in filenames should get quoted on Windows.
  writer.set_escape_platform(ESCAPE_PLATFORM_WIN);
  {
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/foo bar.cc"));
    EXPECT_EQ("\"../../foo/foo$ bar.cc\"", out);
  }

  // Spaces in filenames should get escaped on Posix.
  writer.set_escape_platform(ESCAPE_PLATFORM_POSIX);
  {
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/foo bar.cc"));
    EXPECT_EQ("../../foo/foo\\$ bar.cc", out);
  }

  // Quotes should get blackslash-escaped on Windows and Posix.
  writer.set_escape_platform(ESCAPE_PLATFORM_WIN);
  {
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/\"foobar\".cc"));
    // Our Windows code currently quotes the whole thing in this case for
    // code simplicity, even though it's strictly unnecessary. This might
    // change in the future.
    EXPECT_EQ("\"../../foo/\\\"foobar\\\".cc\"", out);
  }
  writer.set_escape_platform(ESCAPE_PLATFORM_POSIX);
  {
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/\"foobar\".cc"));
    EXPECT_EQ("../../foo/\\\"foobar\\\".cc", out);
  }

  // Backslashes should get escaped on non-Windows and preserved on Windows.
  writer.set_escape_platform(ESCAPE_PLATFORM_WIN);
  {
    StringOutputBuffer out;
    writer.WriteFile(out, OutputFile("foo\\bar.cc"));
    EXPECT_EQ("foo\\bar.cc", out);
  }
  writer.set_escape_platform(ESCAPE_PLATFORM_POSIX);
  {
    StringOutputBuffer out;
    writer.WriteFile(out, OutputFile("foo\\bar.cc"));
    EXPECT_EQ("foo\\\\bar.cc", out);
  }
}

TEST(PathOutput, InhibitQuoting) {
  SourceDir build_dir("//out/Debug/");
  std::string_view source_root("/source/root");
  PathOutput writer(build_dir, source_root, ESCAPE_NINJA_COMMAND);
  writer.set_inhibit_quoting(true);

  writer.set_escape_platform(ESCAPE_PLATFORM_WIN);
  {
    // We should get unescaped spaces in the output with no quotes.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/foo bar.cc"));
    EXPECT_EQ("../../foo/foo$ bar.cc", out);
  }

  writer.set_escape_platform(ESCAPE_PLATFORM_POSIX);
  {
    // Escapes the space.
    StringOutputBuffer out;
    writer.WriteFile(out, SourceFile("//foo/foo bar.cc"));
    EXPECT_EQ("../../foo/foo\\$ bar.cc", out);
  }
}

TEST(PathOutput, WriteDir) {
  {
    SourceDir build_dir("//out/Debug/");
    std::string_view source_root("/source/root");
    PathOutput writer(build_dir, source_root, ESCAPE_NINJA);
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//foo/bar/"),
                      PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("../../foo/bar/", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//foo/bar/"),
                      PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ("../../foo/bar", out);
    }

    // Output source root dir.
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//"), PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("../../", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//"), PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ("../..", out);
    }

    // Output system root dir.
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("/"), PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("/", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("/"), PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("/", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("/"), PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ("/.", out);
    }

    // Output inside current dir.
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//out/Debug/"),
                      PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("./", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//out/Debug/"),
                      PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ(".", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//out/Debug/foo/"),
                      PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("foo/", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, SourceDir("//out/Debug/foo/"),
                      PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ("foo", out);
    }

    // WriteDir using an OutputFile.
    {
      StringOutputBuffer out;
      writer.WriteDir(out, OutputFile("foo/"),
                      PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("foo/", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, OutputFile("foo/"), PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ("foo", out);
    }
    {
      StringOutputBuffer out;
      writer.WriteDir(out, OutputFile(), PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("", out);
    }
  }
  {
    // Empty build dir writer.
    std::string_view source_root("/source/root");
    PathOutput root_writer(SourceDir("//"), source_root, ESCAPE_NINJA);
    {
      StringOutputBuffer out;
      root_writer.WriteDir(out, SourceDir("//"),
                           PathOutput::DIR_INCLUDE_LAST_SLASH);
      EXPECT_EQ("./", out);
    }
    {
      StringOutputBuffer out;
      root_writer.WriteDir(out, SourceDir("//"), PathOutput::DIR_NO_LAST_SLASH);
      EXPECT_EQ(".", out);
    }
  }
}
