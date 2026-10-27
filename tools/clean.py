#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A cross-platform rm -rf path/to/dir/*.

We specifically don't do rm -rf path/to/dir, because that would change the
mtime of path/to, triggering ninja to rebuild.
"""

import os
import shutil
import sys

target = sys.argv[1]
if os.path.isdir(target):
  for entry in os.scandir(target):
    if entry.is_dir(follow_symlinks=False):
      shutil.rmtree(entry.path)
    else:
      os.unlink(entry.path)
