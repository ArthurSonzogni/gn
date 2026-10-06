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
  ensure_file=$(mktemp)
  # https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/clang
  # shellcheck disable=SC2016
  echo 'fuchsia/third_party/clang/${platform} integration' > "$ensure_file"
  trap 'rm "$ensure_file"' EXIT
  cipd ensure -ensure-file "$ensure_file" -root clang
  CLANG_FORMAT="./clang/bin/clang-format"
fi

git ls-files | grep -E '\.(h|cc)$' | xargs "$CLANG_FORMAT" "${clang_format_opts[@]}"
