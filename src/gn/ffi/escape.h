// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FFI_ESCAPE_H_
#define TOOLS_GN_FFI_ESCAPE_H_

#include <stdint.h>

#include <string_view>

#include "cxx.h"
#include "gn/escape.h"

inline uint8_t* EscapeNinja(rust::Str str, uint8_t* out) {
  return reinterpret_cast<uint8_t*>(
      Escaped::Ninja{std::string_view(str)}.WriteToBuf(
          reinterpret_cast<char*>(out)));
}

inline uint8_t* EscapeNinjaCommandPosix(rust::Str str, uint8_t* out) {
  return reinterpret_cast<uint8_t*>(
      Escaped::Command{std::string_view(str), ESCAPE_PLATFORM_POSIX}.WriteToBuf(
          reinterpret_cast<char*>(out)));
}

inline uint8_t* EscapeNinjaCommandWindows(rust::Str str, uint8_t* out) {
  return reinterpret_cast<uint8_t*>(
      Escaped::Command{std::string_view(str), ESCAPE_PLATFORM_WIN}.WriteToBuf(
          reinterpret_cast<char*>(out)));
}

#endif  // TOOLS_GN_FFI_ESCAPE_H_
