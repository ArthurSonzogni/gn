// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_RESOLVED_TARGET_DATA_H_
#define TOOLS_GN_RESOLVED_TARGET_DATA_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "base/containers/span.h"
#include "gn/lib_file.h"
#include "gn/output_file.h"
#include "gn/resolved_target_deps.h"
#include "gn/source_dir.h"
#include "gn/target.h"
#include "gn/target_public_pair.h"
#include "gn/unique_vector.h"

// A class used to compute target-specific data by collecting information
// from its tree of dependencies.
//
// For example, linkable targets can call GetLinkedLibraries() and
// GetLinkedLibraryDirs() to find the library files and library search
// paths to add to their final linker command string, based on the
// definitions of the `libs` and `lib_dirs` config values of their
// transitive dependencies.
//
// Values are computed on demand, but memorized by the class instance in order
// to speed up multiple queries for targets that share dependencies.
//
// Usage is:
//
//  1) Create instance.
//
//  2) Call any of the methods to retrieve the value of the corresponding
//     data. For all methods, the input Target instance passed as argument
//     must have been fully resolved (meaning that Target::OnResolved()
//     must have been called and completed). Input target pointers are
//     const and thus are never modified. This allows using multiple
//     ResolvedTargetData instances from the same input graph in multiple
//     threads safely.
//
class ResolvedTargetData {
 public:
  // Return the public/private/data/dependencies of a given target
  // as a ResolvedTargetDeps instance.
  const ResolvedTargetDeps& GetTargetDeps(const Target* target) const {
    return GetTargetInfo(target)->deps;
  }

  // Return the data dependencies of a given target.
  // Convenience shortcut for GetTargetDeps(target).data_deps().
  base::span<const Target*> GetDataDeps(const Target* target) const {
    return GetTargetDeps(target).data_deps();
  }

  // Return the public and private dependencies of a given target.
  // Convenience shortcut for GetTargetDeps(target).linked_deps().
  base::span<const Target*> GetLinkedDeps(const Target* target) const {
    return GetTargetDeps(target).linked_deps();
  }

  // The list of all library directory search path to add to the final link
  // command of linkable binary. For example, if this returns ['dir1', 'dir2']
  // a command for a C++ linker would typically use `-Ldir1 -Ldir2`.
  const std::vector<SourceDir>& GetLinkedLibraryDirs(
      const Target* target) const {
    return GetTargetLibInfo(target)->lib_dirs;
  }

  // The list of all library files to add to the final link command of linkable
  // binaries. For example, if this returns ['foo', '/path/to/bar'], the command
  // for a C++ linker would typically use '-lfoo /path/to/bar'.
  const std::vector<LibFile>& GetLinkedLibraries(const Target* target) const {
    return GetTargetLibInfo(target)->libs;
  }

  // The list of framework directories search paths to use at link time
  // when generating macOS or iOS linkable binaries.
  const std::vector<SourceDir>& GetLinkedFrameworkDirs(
      const Target* target) const {
    return GetTargetFrameworkInfo(target)->framework_dirs;
  }

  // The list of framework names to use at link time when generating macOS
  // or iOS linkable binaries.
  const std::vector<std::string>& GetLinkedFrameworks(
      const Target* target) const {
    return GetTargetFrameworkInfo(target)->frameworks;
  }

  // The list of weak framework names to use at link time when generating macOS
  // or iOS linkable binaries.
  const std::vector<std::string>& GetLinkedWeakFrameworks(
      const Target* target) const {
    return GetTargetFrameworkInfo(target)->weak_frameworks;
  }

  // The list of weak library files to use at link time when generating macOS
  // or iOS linkable binaries.
  const std::vector<std::string>& GetLinkedWeakLibraries(
      const Target* target) const {
    return GetTargetFrameworkInfo(target)->weak_libraries;
  }

  // Retrieves a set of hard dependencies for this target.
  // All hard deps from this target and all dependencies, but not the
  // target itself.
  const TargetSet& GetHardDeps(const Target* target) const {
    return GetTargetHardDeps(target)->hard_deps;
  }

  // Retrieves an ordered list of (target, is_public) pairs for all link-time
  // libraries inherited by this target.
  const std::vector<TargetPublicPair>& GetInheritedLibraries(
      const Target* target) const {
    return GetTargetInheritedLibs(target)->inherited_libs;
  }

  // Retrieves an ordered list of (target, is_public) pairs for all module
  // dependencies inherited by this target.
  const std::vector<TargetPublicPair>& GetModuleDepsInformation(
      const Target* target) const {
    return GetTargetModuleDepsInformation(target)->module_deps_information;
  }

  // Retrieves an ordered list of (target, is_public) paris for all link-time
  // libraries for Rust-specific binary targets.
  const std::vector<TargetPublicPair>& GetRustInheritedLibraries(
      const Target* target) const {
    return GetTargetRustLibs(target)->rust_inherited_libs;
  }

  // List of dependent target that generate a .swiftmodule. The current target
  // is assumed to depend on those modules, and will add them to the module
  // search path.
  base::span<const Target*> GetSwiftModuleDependencies(
      const Target* target) const {
    const TargetInfo* info = GetTargetSwiftValues(target);
    if (!info->swift_values.get())
      return {};
    return info->swift_values->modules;
  }

  // Retrieves an ordered list of all order-only dependency outputs for this
  // target.
  // Returns true if a target depending on |dep| inherits the hard deps of
  // |dep|. Used to compute GetHardDeps(), and by code that needs to mirror
  // that computation.
  static bool ForwardsHardDeps(const Target* dep);

  const std::vector<OutputFile>& GetOrderOnlyDeps(const Target* target) const {
    return GetTargetOrderOnlyDeps(target)->order_only_deps;
  }

  // Returns true if this target exports public inputs, either directly or from
  // public dependencies which do.
  bool ExportsPublicInputs(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    LazyBool value =
        info->does_export_public_inputs.load(std::memory_order_acquire);
    if (value == LazyBool::kUnknown) {
      value =
          ComputeExportsPublicInputs(info) ? LazyBool::kTrue : LazyBool::kFalse;
      info->does_export_public_inputs.store(value, std::memory_order_release);
    }
    return value == LazyBool::kTrue;
  }

 private:
  using LazyBool = Target::LazyBool;
  using TargetInfo = Target::TargetInfo;

  // Retrieve TargetInfo value associated with |target|.
  TargetInfo* GetTargetInfo(const Target* target) const {
    return &target->info();
  }

  const TargetInfo* GetTargetLibInfo(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_lib_info.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_lib_info.load(std::memory_order_relaxed)) {
        ComputeLibInfo(info);
        info->has_lib_info.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetFrameworkInfo(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_framework_info.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_framework_info.load(std::memory_order_relaxed)) {
        ComputeFrameworkInfo(info);
        info->has_framework_info.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetHardDeps(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_hard_deps.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_hard_deps.load(std::memory_order_relaxed)) {
        ComputeHardDeps(info);
        info->has_hard_deps.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetInheritedLibs(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_inherited_libs.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_inherited_libs.load(std::memory_order_relaxed)) {
        ComputeInheritedLibs(info);
        info->has_inherited_libs.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetModuleDepsInformation(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_module_deps_information.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_module_deps_information.load(std::memory_order_relaxed)) {
        ComputeModuleDepsInformation(info);
        info->has_module_deps_information.store(true,
                                                std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetRustLibs(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_rust_libs.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_rust_libs.load(std::memory_order_relaxed)) {
        ComputeRustLibs(info);
        info->has_rust_libs.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetSwiftValues(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_swift_values.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_swift_values.load(std::memory_order_relaxed)) {
        ComputeSwiftValues(info);
        info->has_swift_values.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  const TargetInfo* GetTargetOrderOnlyDeps(const Target* target) const {
    TargetInfo* info = GetTargetInfo(target);
    if (!info->has_order_only_deps.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(info->mutex);
      if (!info->has_order_only_deps.load(std::memory_order_relaxed)) {
        ComputeOrderOnlyDeps(info);
        info->has_order_only_deps.store(true, std::memory_order_release);
      }
    }
    return info;
  }

  // Compute the portion of TargetInfo guarded by one of the |has_xxx|
  // booleans. This performs recursive and expensive computations and
  // should only be called once per TargetInfo instance.
  void ComputeLibInfo(TargetInfo* info) const;
  void ComputeFrameworkInfo(TargetInfo* info) const;
  void ComputeHardDeps(TargetInfo* info) const;
  void ComputeInheritedLibs(TargetInfo* info) const;
  void ComputeModuleDepsInformation(TargetInfo* info) const;
  void ComputeRustLibs(TargetInfo* info) const;
  void ComputeSwiftValues(TargetInfo* info) const;
  void ComputeOrderOnlyDeps(TargetInfo* info) const;
  bool ComputeExportsPublicInputs(const TargetInfo* info) const;

  // Helper function used by ComputeInheritedLibs().
  void ComputeInheritedLibsFor(
      base::span<const Target*> deps,
      bool is_public,
      TargetPublicPairListBuilder* inherited_libraries) const;

  // Helper function used by ComputeModuleDepsInformation().
  void ComputeModuleDepsInformationFor(
      base::span<const Target*> deps,
      bool is_public,
      TargetPublicPairListBuilder* module_deps_information) const;

  // Helper data structure and function used by ComputeRustLibs().
  struct RustLibsBuilder {
    TargetPublicPairListBuilder inherited;
    TargetPublicPairListBuilder inheritable;
  };

  void ComputeRustLibsFor(base::span<const Target*> deps,
                          bool is_public,
                          RustLibsBuilder* rust_libs) const;
};

#endif  // TOOLS_GN_RESOLVED_TARGET_DATA_H_
