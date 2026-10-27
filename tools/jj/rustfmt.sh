#!/bin/bash
# Copyright 2026 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu -o pipefail

root="$(dirname "$(dirname "$(dirname "$0")")")"
rustfmt_bin="${RUSTFMT:-${root}/clang/bin/rustfmt}"

# Use nightly rustfmt if available to get advanced formatting (import sorting, etc.)
if rustup run nightly rustfmt --version >/dev/null 2>&1; then
  exec rustup run nightly rustfmt --config-path="${root}/src/gn/starlark/rustfmt-nightly.toml" "$@"
elif [ -x "${rustfmt_bin}" ]; then
  # Otherwise fallback to hermetic rustfmt with standard stable formatting rules
  exec "${rustfmt_bin}" --config-path="${root}/src/gn/starlark/rustfmt.toml" "$@"
else
  # Fallback to no formatting, pipe stdin to stdout
  cat
fi
