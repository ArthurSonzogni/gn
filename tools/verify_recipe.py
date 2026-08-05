#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
  os.chdir(REPO_ROOT)

  parser = argparse.ArgumentParser(
      description='Trigger remote tryjobs with local recipe modifications.')
  parser.add_argument(
      'cl',
      help='Gerrit CL URL or number (e.g. 24801 or https://gn-review.googlesource.com/c/gn/+/24801)'
  )
  parser.add_argument(
      '--platform',
      '-p',
      choices=['linux', 'mac', 'win'],
      action='append',
      help='Platform(s) to run tryjobs for. If omitted, runs all platforms.')

  args = parser.parse_args()

  cl = args.cl
  if cl.isdigit():
    cl = f'https://gn-review.googlesource.com/c/gn/+/{cl}'

  platforms = list(dict.fromkeys(args.platform)) if args.platform else ['linux', 'mac', 'win']

  print(f'Triggering tryjobs for CL: {cl}...')

  for platform in platforms:
    print(f'Triggering {platform} tryjob...')
    p1 = subprocess.Popen(['led', 'get-builder', f'gn/try/{platform}'],
                          stdout=subprocess.PIPE)
    p2 = subprocess.Popen(['led', 'edit-cr-cl', cl],
                          stdin=p1.stdout,
                          stdout=subprocess.PIPE)
    p3 = subprocess.Popen(['led', 'edit-recipe-bundle'],
                          stdin=p2.stdout,
                          stdout=subprocess.PIPE)
    p4 = subprocess.Popen(['led', 'launch'], stdin=p3.stdout)

    p1.stdout.close()
    p2.stdout.close()
    p3.stdout.close()

    for p in [p4, p3, p2, p1]:
      p.wait()
      if p.returncode != 0:
        print(f'Failed to launch tryjob for {platform}', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
  main()
