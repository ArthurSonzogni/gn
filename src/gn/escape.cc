// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/escape.h"

#include <stddef.h>

#include <array>

#include "base/compiler_specific.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "gn/string_output_buffer.h"
#include "util/build_config.h"

namespace {

#if defined(OS_WIN)
constexpr size_t kMaxEscapedCharsPerChar = 2;
#else
constexpr size_t kMaxEscapedCharsPerChar = 3;
#endif

// A "1" in this lookup table means that char is valid in the Posix shell.
// clang-format off
constexpr char kShellValid[256] = {
// 00-1f: all are invalid
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// ' ' !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
//  0  1  2  3  4  5  6  7  8  9  :  ;  <  =  >  ?
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0,
//  @  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//  P  Q  R  S  T  U  V  W  X  Y  Z  [  \  ]  ^  _
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
//  `  a  b  c  d  e  f  g  h  i  j  k  l  m  n  o
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//  p  q  r  s  t  u  v  w  x  y  z  {  |  }  ~
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
// 80-ff: non-ASCII bytes (e.g. UTF-8 multi-byte sequences) are valid
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
// clang-format on

constexpr std::array<char, 256> PosixEscapesTo(const char (&entries)[256]) {
  std::array<char, 256> table{};
  for (size_t i = 0; i < table.size(); ++i) {
    if (entries[i] == 0) {
      table[i] = '\\';
    }
  }
  table[':'] = '$';
  return table;
}

constexpr auto kPosixNinjaTable = PosixEscapesTo(kShellValid);

constexpr std::array<bool, 256> InitNinjaEscapeTable() {
  std::array<bool, 256> table{};
  table['$'] = true;
  table[' '] = true;
  table[':'] = true;
  return table;
}

constexpr auto kNinjaEscapeTable = InitNinjaEscapeTable();

char* EscapeStringToString_Space(std::string_view str, char* out) {
  size_t in_idx = 0;
  for (size_t delim = str.find(' '); UNLIKELY(delim != std::string_view::npos);
       delim = str.find(' ', in_idx)) {
    size_t len = delim - in_idx;
    memcpy(out, str.data() + in_idx, len);
    out += len;
    *out++ = '\\';
    *out++ = ' ';
    in_idx = delim + 1;
  }
  size_t len = str.size() - in_idx;
  memcpy(out, str.data() + in_idx, len);
  out += len;
  return out;
}

// Ninja's escaping rules are very simple. We always escape colons even
// though they're OK in many places, in case the resulting string is used on
// the left-hand-side of a rule.
inline bool ShouldEscapeCharForNinja(char ch) {
  return kNinjaEscapeTable[static_cast<uint8_t>(ch)];
}

// Performance could be further improved with architecture-specific SIMD
// (e.g. SSE2 on x86, NEON on ARM), but that is platform-dependent, so we
// refrain from doing so for now.
char* EscapeStringToString_Ninja(std::string_view str, char* out) {
  size_t in_idx = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    if (UNLIKELY(ShouldEscapeCharForNinja(str[i]))) {
      size_t len = i - in_idx;
      memcpy(out, str.data() + in_idx, len);
      out += len;
      *out++ = '$';
      *out++ = str[i];
      in_idx = i + 1;
    }
  }
  size_t len = str.size() - in_idx;
  memcpy(out, str.data() + in_idx, len);
  out += len;
  return out;
}

inline bool ShouldEscapeCharForCompilationDatabase(char ch) {
  return ch == '\\' || ch == '"';
}

size_t EscapeStringToString_CompilationDatabase(std::string_view str,
                                                char* dest) {
  size_t i = 0;
  bool quote = false;
  for (const auto& elem : str) {
    if (!kShellValid[static_cast<uint8_t>(elem)]) {
      quote = true;
      break;
    }
  }
  if (quote)
    dest[i++] = '"';

  for (const auto& elem : str) {
    if (ShouldEscapeCharForCompilationDatabase(elem))
      dest[i++] = '\\';
    dest[i++] = elem;
  }
  if (quote)
    dest[i++] = '"';
  return i;
}

size_t EscapeStringToString_Depfile(std::string_view str, char* dest) {
  size_t i = 0;
  for (const auto& elem : str) {
    // Escape all characters that ninja depfile parser can recognize as escaped,
    // even if some of them can work without escaping.
    if (elem == ' ' || elem == '\\' || elem == '#' || elem == '*' ||
        elem == '[' || elem == '|' || elem == ']')
      dest[i++] = '\\';
    else if (elem == '$')  // Extra rule for $$
      dest[i++] = '$';
    dest[i++] = elem;
  }
  return i;
}

size_t EscapeStringToString_NinjaPreformatted(std::string_view str,
                                              char* dest) {
  // Only Ninja-escape $.
  size_t i = 0;
  for (const auto& elem : str) {
    if (elem == '$')
      dest[i++] = '$';
    dest[i++] = elem;
  }
  return i;
}

// Escape for CommandLineToArgvW and additionally escape Ninja characters.
//
// The basic algorithm is if the string doesn't contain any parse-affecting
// characters, don't do anything (other than the Ninja processing). If it does,
// quote the string, and backslash-escape all quotes and backslashes.
// See:
//   http://blogs.msdn.com/b/twistylittlepassagesallalike/archive/2011/04/23/everyone-quotes-arguments-the-wrong-way.aspx
//   http://blogs.msdn.com/b/oldnewthing/archive/2010/09/17/10063629.aspx
size_t EscapeStringToString_WindowsNinjaFork(std::string_view str,
                                             bool inhibit_quoting,
                                             char* dest,
                                             bool* needed_quoting) {
  // We assume we don't have any whitespace chars that aren't spaces.
  DCHECK(str.find_first_of("\r\n\v\t") == std::string::npos);

  size_t i = 0;
  // '$', ' ', and ':' mean we need ninja escaping. ' ' and '"' mean we need
  // shell escaping.
  size_t idx = str.find_first_of("$: \"");
  if (LIKELY(idx == std::string::npos)) {
    // Super-fast-path: No shell escaping or Ninja escaping required.
    memcpy(dest, str.data(), str.size());
    return str.size();
  }

  // If the special character found was NOT a space or quote (i.e. it was '$'
  // or ':'), check if there are any spaces or quotes in the rest of the string.
  if (str[idx] != ' ' && str[idx] != '"' &&
      str.find_first_of(" \"", idx + 1) == std::string::npos) {
    // Fast-path: Needs Ninja escaping, but no Windows command-line quoting.
    // We know that the up until here doesn't require any escaping
    memcpy(dest, str.data(), idx);
    char* out = &dest[idx];
    // Ninja always escapes with a $
    *out++ = '$';
    *out++ = str[idx];
    // Escape the rest of the string with ninja.
    out = EscapeStringToString_Ninja(str.substr(idx + 1), out);
    return out - dest;
  }

  if (!inhibit_quoting)
    dest[i++] = '"';

  for (size_t j = 0; j < str.size(); j++) {
    // Count backslashes in case they're followed by a quote.
    size_t backslash_count = 0;
    while (j < str.size() && str[j] == '\\') {
      j++;
      backslash_count++;
    }
    if (j == str.size()) {
      // Backslashes at end of string. Backslash-escape all of them since
      // they'll be followed by a quote.
      memset(dest + i, '\\', backslash_count * 2);
      i += backslash_count * 2;
    } else if (str[j] == '"') {
      // 0 or more backslashes followed by a quote. Backslash-escape the
      // backslashes, then backslash-escape the quote.
      memset(dest + i, '\\', backslash_count * 2 + 1);
      i += backslash_count * 2 + 1;
      dest[i++] = '"';
    } else {
      // Non-special Windows character, just escape for Ninja. Also, add any
      // backslashes we read previously, these are literals.
      memset(dest + i, '\\', backslash_count);
      i += backslash_count;
      if (ShouldEscapeCharForNinja(str[j]))
        dest[i++] = '$';
      dest[i++] = str[j];
    }
  }

  if (!inhibit_quoting)
    dest[i++] = '"';
  if (needed_quoting)
    *needed_quoting = true;

  return i;
}

char* EscapeStringToString_PosixNinjaFork(std::string_view str, char* out) {
  size_t start = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    char esc = kPosixNinjaTable[static_cast<uint8_t>(str[i])];
    if (UNLIKELY(esc != '\0')) {
      size_t len = i - start;
      memcpy(out, str.data() + start, len);
      out += len;
      char c = str[i];
      *out++ = esc;
      if (c == ' ' || c == '$') {
        *out++ = '$';
      }
      *out++ = c;
      start = i + 1;
    }
  }
  size_t len = str.size() - start;
  memcpy(out, str.data() + start, len);
  out += len;
  return out;
}

// Escapes |str| into |dest| and returns the number of characters written.
size_t EscapeStringToString(std::string_view str,
                            const EscapeOptions& options,
                            char* dest,
                            bool* needed_quoting) {
  switch (options.mode) {
    case ESCAPE_NONE:
      strncpy(dest, str.data(), str.size());
      return str.size();
    case ESCAPE_SPACE:
      return EscapeStringToString_Space(str, dest) - dest;
    case ESCAPE_NINJA:
      return EscapeStringToString_Ninja(str, dest) - dest;
    case ESCAPE_DEPFILE:
      return EscapeStringToString_Depfile(str, dest);
    case ESCAPE_COMPILATION_DATABASE:
      return EscapeStringToString_CompilationDatabase(str, dest);
    case ESCAPE_NINJA_COMMAND:
      switch (options.platform) {
        case ESCAPE_PLATFORM_WIN:
          return EscapeStringToString_WindowsNinjaFork(
              str, options.inhibit_quoting, dest, needed_quoting);
        case ESCAPE_PLATFORM_POSIX:
          return EscapeStringToString_PosixNinjaFork(str, dest) - dest;
        default:
          NOTREACHED();
      }
    case ESCAPE_NINJA_PREFORMATTED_COMMAND:
      return EscapeStringToString_NinjaPreformatted(str, dest);
    default:
      NOTREACHED();
  }
  return 0;
}

}  // namespace

char* Escaped::Space::WriteToBuf(char* out) const {
  return EscapeStringToString_Space(str, out);
}

char* Escaped::Ninja::WriteToBuf(char* out) const {
  return EscapeStringToString_Ninja(str, out);
}

char* Escaped::Depfile::WriteToBuf(char* out) const {
  return &out[EscapeStringToString_Depfile(str, out)];
}

char* Escaped::CompilationDatabase::WriteToBuf(char* out) const {
  return &out[EscapeStringToString_CompilationDatabase(str, out)];
}

char* Escaped::NinjaPreformatted::WriteToBuf(char* out) const {
  return &out[EscapeStringToString_NinjaPreformatted(str, out)];
}

char* Escaped::Command::WriteToBuf(char* out) const {
  switch (platform) {
    case ESCAPE_PLATFORM_WIN:
      return &out[EscapeStringToString_WindowsNinjaFork(str, inhibit_quoting,
                                                        out, nullptr)];
    case ESCAPE_PLATFORM_POSIX:
      return EscapeStringToString_PosixNinjaFork(str, out);
    default:
      NOTREACHED();
  }
  return out;
}

std::string EscapeString(std::string_view str,
                         const EscapeOptions& options,
                         bool* needed_quoting) {
  std::string result;
  result.resize(str.size() * kMaxEscapedCharsPerChar + 2);
  size_t len =
      EscapeStringToString(str, options, result.data(), needed_quoting);
  result.resize(len);
  return result;
}

void EscapeStringToStream(StringOutputBuffer& out,
                          std::string_view str,
                          const EscapeOptions& options) {
  switch (options.mode) {
    case ESCAPE_NONE:
      out << str;
      break;
    case ESCAPE_SPACE:
      out << Escaped::Space(str);
      break;
    case ESCAPE_NINJA:
      out << Escaped::Ninja(str);
      break;
    case ESCAPE_DEPFILE:
      out << Escaped::Depfile(str);
      break;
    case ESCAPE_COMPILATION_DATABASE:
      out << Escaped::CompilationDatabase(str);
      break;
    case ESCAPE_NINJA_COMMAND:
      out << Escaped::Command(str, options.platform, options.inhibit_quoting);
      break;
    case ESCAPE_NINJA_PREFORMATTED_COMMAND:
      out << Escaped::NinjaPreformatted(str);
      break;
    default:
      NOTREACHED();
  }
}

void EscapeJSONStringToStream(StringOutputBuffer& out,
                              std::string_view str,
                              const EscapeOptions& options) {
  std::string dest;
  bool needed_quoting = !options.inhibit_quoting;
  base::EscapeJSONString(str, needed_quoting, &dest);

  EscapeStringToStream(out, dest, options);
}
