// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/string_output_buffer.h"

#include <fstream>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "gn/err.h"
#include "gn/file_writer.h"
#include "gn/filesystem_utils.h"

void StringOutputBuffer::Clear() {
  ro_.clear();
  cur_ = rw_buf_.get();
}

void StringOutputBuffer::ReserveSlow(size_t n) {
  if (rw_buf_) {
    size_t page_size = cur_ - rw_buf_.get();
    if (page_size > 0) {
      ro_.push_back(Page{
          .buf = std::move(rw_buf_),
          .size = page_size,
      });
    }
  }

  // Pick the minimum number of pages required to store n bytes.
  // So technically, our "Page" could actually be multiple pages
  // But unless you attempt to call append on a ridiculously large string,
  // this should never happen in practice.
  size_t capacity = (n + kPageSize - 1) & ~(kPageSize - 1);
  rw_buf_ = std::make_unique_for_overwrite<char[]>(capacity);
  cur_ = rw_buf_.get();
  end_ = cur_ + capacity;
}

size_t StringOutputBuffer::size() const {
  size_t total = rw_buf_ ? static_cast<size_t>(cur_ - rw_buf_.get()) : 0;
  for (const auto& page : ro_) {
    total += page.size;
  }
  return total;
}

std::string StringOutputBuffer::str() const {
  std::string result;
  result.reserve(size());
  for (const auto& page : ro_) {
    result.append(page.buf.get(), page.size);
  }
  if (rw_buf_) {
    result.append(rw_buf_.get(), cur_);
  }
  return result;
}

bool StringOutputBuffer::ContentsEqual(const base::FilePath& file_path) const {
  // Compare file and stream sizes first. Quick and will save us some time if
  // they are different sizes.
  int64_t file_size;
  if (!base::GetFileSize(file_path, &file_size) ||
      static_cast<size_t>(file_size) != size()) {
    return false;
  }

  // Open the file in binary mode.
  std::ifstream file(file_path.As8Bit().c_str(), std::ios::binary);
  if (!file.is_open())
    return false;

  std::vector<char> file_page;
  file_page.resize(kPageSize);
  auto check_page = [&](const char* page_buf, size_t page_size) {
    // Don't use reserve because if we do that, capacity is high enough but not
    // size.
    if (UNLIKELY(file_page.size() < page_size)) {
      file_page.resize(page_size);
    }
    file.read(file_page.data(), page_size);
    if (!file.good())
      return false;

    return memcmp(file_page.data(), page_buf, page_size) == 0;
  };

  for (const auto& page : ro_) {
    if (!check_page(page.buf.get(), page.size))
      return false;
  }
  if (cur_ > rw_buf_.get()) {
    return check_page(rw_buf_.get(), cur_ - rw_buf_.get());
  }
  return true;
}

// Write the contents of this instance to a file at |file_path|.
bool StringOutputBuffer::WriteToFile(const base::FilePath& file_path,
                                     Err* err) const {
  // Create the directory if necessary.
  if (!base::CreateDirectory(file_path.DirName())) {
    if (err) {
      *err =
          Err(Location(), "Unable to create directory.",
              "I was using \"" + FilePathToUTF8(file_path.DirName()) + "\".");
    }
    return false;
  }

  FileWriter writer;
  bool success = writer.Create(file_path);
  if (success) {
    for (const auto& page : ro_) {
      success = writer.Write(std::string_view(page.buf.get(), page.size));
      if (!success)
        break;
    }
    if (success && rw_buf_) {
      success = writer.Write(std::string_view(rw_buf_.get(), cur_));
    }
  }
  if (!writer.Close())
    success = false;

  if (!success && err) {
    *err = Err(Location(), "Unable to write file.",
               "I was writing \"" + FilePathToUTF8(file_path) + "\".");
  }
  return success;
}

bool StringOutputBuffer::WriteToFileIfChanged(const base::FilePath& file_path,
                                              Err* err) const {
  if (ContentsEqual(file_path))
    return true;

  return WriteToFile(file_path, err);
}

std::string Pretty(const StringOutputBuffer& buffer) {
  return buffer.str();
}
