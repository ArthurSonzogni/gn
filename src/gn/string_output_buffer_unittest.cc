// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/string_output_buffer.h"

#include <limits>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "util/test/test.h"

namespace {

// Create a test string of |size| characters with pseudo-random ASCII content.
std::string CreateTestString(size_t size, size_t seed = 0) {
  std::string result;
  result.resize(size);
  for (size_t n = 0; n < size; ++n) {
    int offset = (size + seed + n * 1337);
    char ch = ' ' + offset % (127 - 32);
    result[n] = ch;
  }
  return result;
}

}  // namespace

TEST(StringOutputBuffer, Append) {
  const size_t data_size = 100000;
  std::string data = CreateTestString(data_size);

  const size_t num_spans = 50;
  const size_t span_size = data_size / num_spans;

  StringOutputBuffer buffer;

  for (size_t n = 0; n < num_spans; ++n) {
    size_t start_offset = n * span_size;
    size_t end_offset = std::min(start_offset + span_size, data.size());
    buffer.Append(&data[start_offset], end_offset - start_offset);
  }

  EXPECT_EQ(data.size(), buffer.size());
  ASSERT_STREQ(data.c_str(), buffer.str().c_str());
}

TEST(StringOutputBuffer, AppendWithPageSizeMultiples) {
  const size_t page_size = StringOutputBuffer::GetPageSizeForTesting();
  const size_t page_count = 100;
  const size_t data_size = page_size * page_count;
  std::string data = CreateTestString(data_size);

  StringOutputBuffer buffer;

  for (size_t n = 0; n < page_count; ++n) {
    size_t start_offset = n * page_size;
    buffer.Append(&data[start_offset], page_size);
  }

  EXPECT_EQ(data.size(), buffer.size());
  ASSERT_STREQ(data.c_str(), buffer.str().c_str());
}

TEST(StringOutputBuffer, Clear) {
  StringOutputBuffer buffer;
  buffer << "first string";
  EXPECT_EQ("first string", buffer.str());
  EXPECT_EQ(12u, buffer.size());

  buffer.Clear();
  EXPECT_EQ("", buffer.str());
  EXPECT_EQ(0u, buffer.size());

  buffer << "second string";
  EXPECT_EQ("second string", buffer.str());
  EXPECT_EQ(13u, buffer.size());
}

TEST(StringOutputBuffer, ContentsEqual) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const size_t data_size = 100000;
  std::string elem = CreateTestString(data_size);
  std::string data = elem + elem;

  base::FilePath file_path = temp_dir.GetPath().AppendASCII("foo.txt");
  base::WriteFile(file_path, data.c_str(), static_cast<int>(data.size()));

  {
    StringOutputBuffer buffer;
    // If we just call buffer.Append(data), then it remains in the same
    // allocation, and thus our code isn't tested properly.
    buffer.Append(elem);
    buffer.Append(elem);
    EXPECT_EQ(data, buffer);

    EXPECT_TRUE(buffer.ContentsEqual(file_path));

    // Different length and contents.
    buffer << "extra";
    EXPECT_FALSE(buffer.ContentsEqual(file_path));
  }

  // The same length, different contents.
  {
    StringOutputBuffer buffer;
    buffer << CreateTestString(data_size, 1);

    EXPECT_FALSE(buffer.ContentsEqual(file_path));
  }
}

TEST(StringOutputBuffer, WriteToFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const size_t data_size = 100000;
  std::string data = CreateTestString(data_size);

  // Write if file doesn't exist. Also create directory.
  base::FilePath file_path =
      temp_dir.GetPath().AppendASCII("bar").AppendASCII("foo.txt");

  StringOutputBuffer buffer;
  buffer.Append(data);

  EXPECT_TRUE(buffer.WriteToFile(file_path, nullptr));

  // Verify file was created and has same content.
  base::File::Info file_info;
  ASSERT_TRUE(base::GetFileInfo(file_path, &file_info));
  ASSERT_TRUE(buffer.ContentsEqual(file_path));
}

namespace {

struct CustomGreeting {
  std::string_view name;
};

struct Unwritable {};

size_t MaximumSize(const CustomGreeting& greeting) {
  return greeting.name.size() + 7;
}

void WriteToBuf(const CustomGreeting& greeting, char* buf, size_t& size) {
  constexpr std::string_view prefix = "Hello, ";
  memcpy(buf + size, prefix.data(), prefix.size());
  size += prefix.size();
  memcpy(buf + size, greeting.name.data(), greeting.name.size());
  size += greeting.name.size();
}

static_assert(Writable<CustomGreeting>);
static_assert(Writable<std::string_view>);
static_assert(Writable<char>);
static_assert(Writable<std::string>);
static_assert(Writable<const char*>);
static_assert(Writable<int>);
static_assert(Writable<int64_t>);
static_assert(Writable<uint64_t>);
static_assert(!Writable<bool>);
static_assert(!Writable<Unwritable>);

}  // namespace

TEST(StringOutputBuffer, CustomWritable) {
  StringOutputBuffer buffer;
  buffer << CustomGreeting{"world "} << std::numeric_limits<int>::min() << " "
         << std::numeric_limits<int>::max() << '!';
  EXPECT_EQ("Hello, world -2147483648 2147483647!", buffer);
}