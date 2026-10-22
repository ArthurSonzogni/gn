#!/bin/bash -eu

# Reformat the C++ and Rust sources.

# Assume this is under tools/.
cd "$(dirname "$(dirname "$0")")"

if [ "${1:-}" = "--diff" ]; then
  clang_format_opts=(--dry-run -Werror)
  fmt_opts=(--check)
else
  clang_format_opts=(-i)
  fmt_opts=()
fi

if [ -z "${CLANG_FORMAT:-}" ]; then
  # If clang/bin/clang-format doesn't exist, downloading it is safe.
  # If it already exists, however, cipd ensure will remove all packages we
  # didn't request (eg. sysroot)
  # Since this updates anyway whenever you run build/gen.py, which reruns
  # whenever pinned.py is changed, when the pinned version changes, it
  # should be picked up whenever you run `ninja -C out run_formatter` (or any
  # ninja command).
  if [ ! -x "clang/bin/clang-format" ]; then
    python3 build/pinned.py
  fi
  CLANG_FORMAT="clang/bin/clang-format"
fi

git ls-files | grep -E '\.(h|cc)$' | xargs "$CLANG_FORMAT" "${clang_format_opts[@]}"
