# Copyright 2026 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Pinned CIPD package versions and toolchains for GN."""

import argparse
import dataclasses
import json
import pathlib
import shutil
import subprocess
from typing import Optional

from target import Arch, Platform, Target

# https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/clang
# https://llvm.googlesource.com/llvm-project
_LLVM_REVISION = '3493720eca95cf844a8d7e58fdd12e0e5644e7d0'
_CLANG_VERSION = f'git_revision:{_LLVM_REVISION}'

# https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/sysroot/linux
_SYSROOT_LINUX_VERSION = 'git_revision:c912d089c3d46d8982fdef76a50514cca79b6132'

# https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/sysroot/focal
_SYSROOT_FOCAL_VERSION = 'git_revision:fa7a5a9710540f30ff98ae48b62f2cdf72ed2acd'

_HOST_CLANG = 'fuchsia/third_party/clang/${platform}'
_HOST_RUST = 'fuchsia/third_party/rust/host/${platform}'

# Rust CIPD packages are tagged with `git_revisions:<rust_commit>,<llvm_commit>`
# because they bundle an LLVM backend. We select the Rust toolchain that matches
# the LLVM revision of Clang for cross-language LTO across the FFI boundary.
#
# See:
# * https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/rust/host
# * https://chrome-infra-packages.appspot.com/p/fuchsia/third_party/rust/target
_RUST_REVISION = 'c26ce708de5d14682647895d2f3caf38f70b5aa6'
_RUST_VERSION = f'git_revisions:{_RUST_REVISION},{_LLVM_REVISION}'

_REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
_DEFAULT_INSTALL_DIR = _REPO_ROOT / 'clang'


@dataclasses.dataclass(frozen=True)
class Package:
  name: str
  version: str
  path: str = ''


@dataclasses.dataclass(frozen=True)
class Toolchain:
  packages: list[Package]
  sysroot: Optional[pathlib.Path]
  env: dict[str, str]


def get_toolchain(host: Target,
                  target: Target,
                  cipd_dir: pathlib.Path = _DEFAULT_INSTALL_DIR) -> Toolchain:
  """Returns the Toolchain required to cross-compile host to target."""
  packages = []
  env = {}
  sysroot = None

  if host.platform.is_linux() or host.platform.is_darwin():
    packages.append(Package(_HOST_CLANG, _CLANG_VERSION))

    env.update({
        'CC': str(cipd_dir / 'bin' / 'clang'),
        'CXX': str(cipd_dir / 'bin' / 'clang++'),
        'AR': str(cipd_dir / 'bin' / 'llvm-ar'),
        'LD': str(cipd_dir / 'bin' / 'clang++'),
    })

  if target.platform.is_linux():
    # RISCV64 support starts in focal.
    if target.arch == Arch.RISCV64:
      packages.append(
          Package('fuchsia/third_party/sysroot/focal', _SYSROOT_FOCAL_VERSION,
                  'sysroot-focal'))
      sysroot = cipd_dir / 'sysroot-focal'
    else:
      packages.append(
          Package('fuchsia/third_party/sysroot/linux', _SYSROOT_LINUX_VERSION,
                  'sysroot'))
      sysroot = cipd_dir / 'sysroot'

  packages.append(Package(_HOST_RUST, _RUST_VERSION))
  packages.append(
    Package(f'fuchsia/third_party/rust/target/{target.rust_triple()}',
            _RUST_VERSION))
  env['CARGO'] = str(cipd_dir / 'bin' / f'cargo{host.platform.exe_suffix}')

  return Toolchain(
      packages=packages,
      sysroot=sysroot,
      env=env,
  )


def install(packages: list[Package]):
  """Installs packages locally via `cipd ensure`."""
  cipd_bin = shutil.which('cipd')
  if not cipd_bin:
    raise FileNotFoundError('cipd executable not found in PATH')

  lines = []
  by_subdir = {}
  for pkg in packages:
    by_subdir.setdefault(pkg.path, []).append(f'{pkg.name} {pkg.version}')

  for subdir, pkgs in by_subdir.items():
    lines.append(f'@Subdir {subdir}\n' if subdir else '@Subdir\n')
    lines.extend([f'{p}\n' for p in pkgs])

  _DEFAULT_INSTALL_DIR.mkdir(exist_ok=True)
  subprocess.run(
      [cipd_bin, 'ensure', '-ensure-file', '-', '-root',
       str(_DEFAULT_INSTALL_DIR)],
      input=''.join(lines),
      text=True,
      check=True,
  )

def _parse_target(target: str):
  os, cpu = target.split('-', 1)
  return Target(Platform(os), Arch(cpu))

def _json_default(obj):
  if dataclasses.is_dataclass(obj):
    return dataclasses.asdict(obj)
  elif isinstance(obj, pathlib.Path):
    return str(obj)
  raise NotImplementedError(repr(obj))


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      '--host',
      type=_parse_target,
      default=Target.host(),
      help='Host platform (e.g. linux-amd64)',
  )
  parser.add_argument(
      '--target',
      nargs='*',
      default=[],
      help='Target platforms',
  )
  parser.add_argument(
      '--ci',
      type=pathlib.Path,
      help='If provided, this is running in CI, installing to this path.',
  )
  args = parser.parse_args()

  targets = args.target if args.target else [args.host]

  if args.ci:
    out = {}
    for t in targets:
      target_obj = _parse_target(t) if isinstance(t, str) else t
      out[t if isinstance(t, str) else str(t)] = get_toolchain(
          args.host, target_obj, cipd_dir=args.ci
      )
    print(json.dumps(out, default=_json_default))
  else:
    install(get_toolchain(host=args.host, target=args.host).packages)

if __name__ == '__main__':
  main()
