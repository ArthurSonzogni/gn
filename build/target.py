# Copyright 2026 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Platform and target architecture descriptors for GN."""

import dataclasses
import enum
import os
import platform as py_platform
import sys

class Arch(enum.Enum):
  """Target CPU architecture."""
  X86_64 = 'x86_64'
  ARM64 = 'arm64'
  RISCV64 = 'riscv64'

  @classmethod
  def _missing_(cls, value):
    if isinstance(value, str):
      s = value.lower()
      if s in ('x86_64', 'amd64', 'x64'):
        return cls.X86_64
      elif s in ('arm64', 'aarch64'):
        return cls.ARM64
      elif s in ('riscv64', 'riscv64gc'):
        return cls.RISCV64
    return super()._missing_(value)

  @classmethod
  def host(cls) -> 'Arch':
    """Returns the host machine architecture."""
    return cls(py_platform.machine())

  @classmethod
  def target(cls) -> 'Arch':
    """Determines the target architecture from CFLAGS/LDFLAGS or falls back to host."""
    cflags = os.environ.get('CFLAGS', '').split()
    ldflags = os.environ.get('LDFLAGS', '').split()

    target_triple = None
    for flag in cflags + ldflags:
      if flag.startswith(('--target=', '-target=')):
        target_triple = flag.split('=', 1)[1]
        break

    if target_triple:
      arch = target_triple.split('-')[0]
      return cls(arch)

    return cls.host()

  def for_cargo(self) -> str:
    """Returns the canonical architecture name used in Cargo target triples."""
    if self == Arch.ARM64:
      return 'aarch64'
    elif self == Arch.RISCV64:
      return 'riscv64gc'
    return self.value


class Platform(object):
  """Represents a host/target platform."""

  def __init__(self, platform=None):
    self._platform = platform
    if self._platform is not None:
      if self._platform in ('darwin', 'mac'):
        self._platform = 'darwin'
      elif self._platform.startswith('win'):
        self._platform = 'msvc'
      return
    self._platform = sys.platform
    if self._platform.startswith('linux'):
      self._platform = 'linux'
    elif self._platform.startswith('darwin'):
      self._platform = 'darwin'
    elif self._platform.startswith('mingw'):
      self._platform = 'mingw'
    elif self._platform.startswith('msys'):
      self._platform = 'msys'
    elif self._platform.startswith('win'):
      self._platform = 'msvc'
    elif self._platform.startswith('aix'):
      self._platform = 'aix'
    elif self._platform.startswith('fuchsia'):
      self._platform = 'fuchsia'
    elif self._platform.startswith('freebsd'):
      self._platform = 'freebsd'
    elif self._platform.startswith('netbsd'):
      self._platform = 'netbsd'
    elif self._platform.startswith('openbsd'):
      self._platform = 'openbsd'
    elif self._platform.startswith('haiku'):
      self._platform = 'haiku'
    elif self._platform.startswith('sunos'):
      self._platform = 'solaris'
    elif self._platform.startswith('zos'):
      self._platform = 'zos'
    elif self._platform.startswith('serenity'):
      self._platform = 'serenity'

  @classmethod
  def host(cls) -> 'Platform':
    return cls()

  @staticmethod
  def known_platforms():
    return [
        'linux', 'darwin', 'mingw', 'msys', 'msvc', 'aix', 'fuchsia', 'freebsd',
        'netbsd', 'openbsd', 'haiku', 'solaris', 'zos', 'serenity'
    ]

  def platform(self):
    return self._platform

  def __str__(self):
    return self._platform

  def is_linux(self):
    return self._platform == 'linux'

  def is_mingw(self):
    return self._platform == 'mingw'

  def is_msys(self):
    return self._platform == 'msys'

  def is_msvc(self):
    return self._platform == 'msvc'

  def is_windows(self):
    return self.is_mingw() or self.is_msvc()

  def is_darwin(self):
    return self._platform == 'darwin'

  def is_aix(self):
    return self._platform == 'aix'

  def is_haiku(self):
    return self._platform == 'haiku'

  def is_solaris(self):
    return self._platform == 'solaris'

  def is_posix(self):
    return self._platform in [
        'linux', 'freebsd', 'darwin', 'aix', 'openbsd', 'haiku', 'solaris',
        'msys', 'netbsd', 'serenity'
    ]

  def is_zos(self):
    return self._platform == 'zos'

  def is_serenity(self):
    return self._platform == 'serenity'

  @property
  def exe_suffix(self):
    return '.exe' if self.is_windows() else ''


@dataclasses.dataclass(frozen=True)
class Target:
  platform: Platform
  arch: Arch

  @classmethod
  def host(cls) -> 'Target':
    return cls(Platform.host(), Arch.host())

  def rust_triple(self) -> str:
    """Returns the Rust target triple for this target."""
    arch_str = self.arch.for_cargo()

    if self.platform.is_linux():
      return f"{arch_str}-unknown-linux-gnu"
    elif self.platform.is_darwin():
      return f"{arch_str}-apple-darwin"
    elif self.platform.is_msvc():
      return f"{arch_str}-pc-windows-msvc"
    elif self.platform.is_mingw() or self.platform.is_msys():
      return f"{arch_str}-pc-windows-gnu"
    else:
      raise NotImplementedError(
          f"Unsupported Rust target platform: {self.platform}")

  def __str__(self):
    return f'{self.platform}-{self.arch.value}'