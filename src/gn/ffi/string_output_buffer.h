// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FFI_STRING_OUTPUT_BUFFER_H_
#define TOOLS_GN_FFI_STRING_OUTPUT_BUFFER_H_

#include <memory>

#include "cxx.h"
#include "gn/string_output_buffer.h"

inline std::unique_ptr<StringOutputBuffer> NewStringOutputBuffer() {
  return std::make_unique<StringOutputBuffer>();
}

inline rust::String StringOutputBufferToString(
    const StringOutputBuffer& buffer) {
  return rust::String(buffer.str());
}

inline char** Reserve(StringOutputBuffer& buffer, size_t n) {
  buffer.Reserve(n);
  return buffer.CurrentPage();
}

#endif  // TOOLS_GN_FFI_STRING_OUTPUT_BUFFER_H_
