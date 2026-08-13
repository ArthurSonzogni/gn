#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to integrate Cargo builds into the Ninja build system.

It is invoked by the Ninja 'cargo' rule to:
1. Run the Cargo build command with a portably configured environment.
2. Locate the compiled artifacts (libraries or test binaries) from Cargo's
   profile directory.
3. Symlink or copy the compiled artifacts to the requested Ninja output path.
4. For test targets: generate a Python test runner script that invokes all unit
   test binaries found in the workspace.
5. Translate and merge Cargo-generated `.d` dependency files (depfiles) into
   a format that Ninja can parse, mapping dependencies relative to the Ninja
   build directory.
"""

import contextlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

_ESC = '\u001e'


def parse_depfile(src_depfile: Path) -> list[Path]:
  """Parses depfile and returns a flattened list of resolved dependency Paths.

  Reads the file, extracts CARGO_MANIFEST_DIR, strips all comment lines,
  cleans up line continuations, and parses and resolves dependencies.
  """
  content = src_depfile.read_text()

  # Strip comments
  content = re.sub(r'(?m)^#.*$', '', content)
  # Strip the output files ("a: b c" -> "b c")
  content = re.sub(r'^[^:]*:\s*', '', content)
  content = content.replace('\\\n', ' ')
  content = content.replace('\\ ', _ESC)

  deps = []

  for f in re.split(r'\s+', content.strip()):
    f = Path(f.replace('\\\\', '\\').replace(_ESC, ' '))
    deps.append(f)

  return deps


def to_depfile_path(p: Path, out_dir: Path):
  # Can't use Path.relative_to because we require walk_up=True, which doesn't
  # exist on preinstalled python on macos.
  rel = os.path.relpath(p, out_dir)
  return rel.replace('\\', '\\\\').replace(' ', '\\ ')


def rewrite_depfile(
    src_depfiles: list[Path], dest_depfile: Path, target_name: str
):
  """Orchestrates reading, parsing, merging, and writing the depfile."""
  all_deps = set()
  for src in src_depfiles:
    if src.exists():
      all_deps.update(parse_depfile(src))
  out_dir = dest_depfile.parent.resolve()
  deps_str = ' '.join(to_depfile_path(d, out_dir) for d in sorted(all_deps))
  new_content = f'{target_name}: {deps_str}\n'

  dest_depfile.parent.mkdir(parents=True, exist_ok=True)
  dest_depfile.write_text(new_content)


def process_lib_target(out_path: Path, cargo_out_dir: Path) -> list[Path]:
  """Maps the static library artifact and returns its source depfiles."""
  lib_name = out_path.name
  depfile_name = f'{out_path.stem}.d'
  src_path = cargo_out_dir / lib_name

  out_path.parent.mkdir(parents=True, exist_ok=True)
  if out_path.exists() or out_path.is_symlink():
    out_path.unlink()
  rel_src = os.path.relpath(src_path, out_path.parent)
  try:
    out_path.symlink_to(rel_src)
  except OSError:
    shutil.copy2(src_path, out_path)

  return [cargo_out_dir / depfile_name]


def process_test_target(out_path: Path, cargo_out_dir: Path) -> list[Path]:
  """Generates a test runner script.

  Returns all test binary source depfiles.
  """
  # When cargo builds tests, it builds one test binary per crate.
  # So we find all those test binaries, then make the generated "test binary"
  # just a script that invokes each of those binaries one by one.

  # Note that the layout of the output directory of cargo has changed. Thus, we
  # must match both:
  # Old layout: deps/<crate>-<hash>(.exe)
  # New layout: build/<crate>/<hash>/out/<crate>-<hash>(.exe)
  layout_pattern = re.compile(
      r'^(?:'
      r'deps'
      r'|'
      r'build/[^/]+/[0-9a-f]{16}/out'
      r')/(?P<crate_name>[a-zA-Z0-9_-]+)-[0-9a-f]{16}(?:\.exe)?$'
  )

  groups = {}
  for root, _, files in os.walk(cargo_out_dir):
    root_path = Path(root)
    for name in files:
      c = root_path / name
      rel_path = c.relative_to(cargo_out_dir).as_posix()
      m = layout_pattern.search(rel_path)
      if not m:
        continue

      # Ensure it is actually an executable test binary
      if (
          c.suffix.lower() != '.exe'
          if sys.platform == 'win32'
          else not os.access(c, os.X_OK)
      ):
        continue

      crate_name = m.group('crate_name')
      # Cargo can cache your test binary under different configurations.
      # E.g. the test binary might be called `mytest-hash1`, then after
      # updating your lockfile, it might keep that binary but future tests
      # would be called `mytest-hash2`. When this happens, we should use the
      # newest one.
      existing = groups.get(crate_name)
      if not existing or c.stat().st_mtime > existing.stat().st_mtime:
        groups[crate_name] = c

  newest_binaries = list(groups.values())

  out_path.parent.mkdir(parents=True, exist_ok=True)
  if out_path.exists() or out_path.is_symlink():
    out_path.unlink()

  runner_content = [
      '#!/usr/bin/env python3',
      'import subprocess',
      'import sys',
      'import os',
      '',
      'tests = [',
  ]
  for b in sorted(newest_binaries):
    rel_path = os.path.relpath(b, out_path.parent)
    runner_content.append(
        f'    os.path.join(os.path.dirname(__file__), {repr(rel_path)}),'
    )
  runner_content.extend([
      ']',
      '',
      'failed = False',
      'for test in tests:',
      '    res = subprocess.run([test] + sys.argv[1:])',
      '    if res.returncode != 0:',
      '        failed = True',
      'if failed:',
      '    sys.exit(1)',
  ])

  out_path.write_text('\n'.join(runner_content) + '\n')
  out_path.chmod(0o755)

  return [b.with_suffix('.d') for b in newest_binaries]


def main():
  if len(sys.argv) < 10:
    print(
        'Usage: run_cargo.py <test|lib> <out> <cargo_out_dir> <cxx> '
        '<cxxflags> <ldflags> <target_triple> <ld> <command...>',
        file=sys.stderr,
    )
    sys.exit(1)

  (
      target_type,
      out_path_str,
      cargo_out_dir_str,
      cxx,
      cxxflags,
      ldflags,
      target_triple,
      linker,
      *cmd_args,
  ) = sys.argv[1:]
  out_path = Path(out_path_str)
  cargo_out_dir = Path(cargo_out_dir_str)

  if sys.platform == 'win32':
    # Strip /GL from Cargo C++ dependencies (like the cxx crate).
    # Rust code compiles to standard COFF objects. If C++ FFI shims generated by Cargo
    # are compiled with /GL, the MSVC linker (running with /LTCG) fails to match the
    # standard COFF Rust references to these C++ symbols during the LTCG phase,
    # causing it to discard the shims as unused and leading to LNK2001 link errors.
    cxxflags = cxxflags.replace('/GL', '')

  # Disable -Wunused-template for cargo C++ dependencies compiled with Clang.
  # Some third-party dependencies (like the `cxx` crate) contain templates that
  # are only used inside asserts. In release builds (with -DNDEBUG), these asserts
  # are compiled away, triggering -Wunused-template causing failures with -Werror.
  if 'clang' in cxx:
    cxxflags += ' -Wno-unused-template'

  os.environ['CXX'] = cxx
  os.environ['CXXFLAGS'] = cxxflags
  # Since Ninja runs commands from the build output directory, CWD is the
  # ninja out dir.
  ninja_out_dir = os.getcwd()
  os.environ['NINJA_OUT_DIR'] = ninja_out_dir
  if sys.platform == 'win32':
    # Linker flags in GN can contain relative paths (e.g. /MANIFESTINPUT)
    # which are relative to the Ninja output directory. Since Cargo runs the
    # linker from different build directories, we must normalize these paths
    # to be absolute.
    def replace_path(match):
      prefix = match.group('prefix')
      path_val = Path(match.group('path'))
      if not path_val.is_absolute():
        abs_path = os.path.normpath(Path(ninja_out_dir) / path_val)
        return f"{prefix}{abs_path}"
      return match.group(0)

    pattern = re.compile(
        r'(?i)(?P<prefix>[-/](?:manifestinput|natvis|pdb|def|implib|libpath):)'
        r'(?P<path>[^\s]+)'
    )
    ldflags = pattern.sub(replace_path, ldflags)

  target_rustflags = [f"-C linker={linker}", f"-L {ninja_out_dir}"] + [
      f"-C link-arg={flag}" for flag in ldflags.split()
  ]

  if sys.platform == 'win32':
    # GN compiles C++ code on Windows with the static C runtime (/MT or /MTd) by
    # default. We must configure rustc to also link statically to prevent linker
    # runtime library mismatch errors (LNK2038).
    target_rustflags.append("-C target-feature=+crt-static")

    # Optimize the ffi crate in debug mode to strip unused cxx shims and
    # resolve MSVC link failures.
    os.environ['CARGO_PROFILE_DEV_PACKAGE_FFI_OPT_LEVEL'] = '1'

    # Compiling the cxx crate requires symlink support.
    # Cargo's internal Git client (libgit2) does not respect environment
    # variables like GIT_CONFIG_COUNT. Instead, we override HOME/USERPROFILE
    # to point to a temporary directory containing a .gitconfig with
    # core.symlinks = true.
    with contextlib.suppress(RuntimeError):
      os.environ.setdefault('CARGO_HOME', str(Path.home() / '.cargo'))

    temp_home = tempfile.mkdtemp()
    with open(os.path.join(temp_home, '.gitconfig'), 'w') as f:
      f.write('[core]\n\tsymlinks = true\n')

    os.environ['USERPROFILE'] = temp_home
    os.environ['HOME'] = temp_home
    os.environ['HOMEDRIVE'] = ''
    os.environ['HOMEPATH'] = ''

  # When linking C++ objects instrumented with ASan/UBSan, we must allow the
  # linker to link its default libraries so it pulls in the sanitizer runtimes.
  if '-fsanitize=' in ldflags:
    target_rustflags.append("-C default-linker-libraries=yes")

  env_var = f"CARGO_TARGET_{target_triple.upper().replace('-', '_')}_RUSTFLAGS"
  os.environ[env_var] = ' '.join(target_rustflags)
  # The link flags are specifically for the target. When cross-compiling, the
  # target and host platforms are the same, but we don't want the link flags
  # when building build tools.
  os.environ['CARGO_TARGET_APPLIES_TO_HOST'] = 'false'

  # Now we run the `cargo build` command
  res = subprocess.run(cmd_args)
  if res.returncode != 0:
    sys.exit(res.returncode)

  # Cargo build doesn't output files in a format ninja can use. So we now
  # need to convert them.
  if target_type == 'clippy':
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.touch()
    sys.exit(0)
  elif target_type == 'lib':
    src_depfiles = process_lib_target(out_path, cargo_out_dir)
  elif target_type == 'test':
    src_depfiles = process_test_target(out_path, cargo_out_dir)
  else:
    raise ValueError(f'Unknown target type: {target_type}')

  dest_depfile = out_path.parent / f'{out_path.name}.d'
  rewrite_depfile(src_depfiles, dest_depfile, out_path)


if __name__ == '__main__':
  main()
