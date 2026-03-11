// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_module_writer_util.h"

#include <algorithm>
#include <set>
#include <utility>

#include "gn/resolved_target_data.h"
#include "gn/substitution_writer.h"
#include "gn/target.h"

ClangModuleDep::ClangModuleDep(const SourceFile* modulemap,
                               const std::string& module_name,
                               std::optional<OutputFile> pcm,
                               bool is_self)
    : modulemap(modulemap),
      module_name(module_name),
      pcm(std::move(pcm)),
      is_self(is_self) {}

std::strong_ordering ClangModuleDep::operator<=>(
    const ClangModuleDep& other) const {
  // Sort by (module name, modulemap path, module file path)
  if (auto cmp = module_name <=> other.module_name; cmp != 0)
    return cmp;
  if (modulemap && other.modulemap) {
    if (auto cmp = *modulemap <=> *other.modulemap; cmp != 0)
      return cmp;
  } else {
    if (auto cmp = modulemap <=> other.modulemap; cmp != 0)
      return cmp;
  }
  // std::optional doesn't support <=> on older versions of mac.
  if (pcm.has_value() && other.pcm.has_value()) {
    return *pcm <=> *other.pcm;
  } else {
    return pcm.has_value() <=> other.pcm.has_value();
  }
}

std::set<ClangModuleDep> GetModuleDepsInformation(
    const Target* target,
    const ResolvedTargetData& resolved) {
  std::set<ClangModuleDep> ret;

  auto add_if_new = [&ret](const Target* t, bool is_self) {
    std::optional<OutputFile> pcm = std::nullopt;
    switch (t->module_type()) {
      case Target::GENERATED_TEXTUAL_MODULEMAP:
        ret.emplace(t->modulemap_file(), t->module_name(), std::nullopt,
                    is_self);
        break;
      case Target::EXPLICIT_MODULEMAP: {
        auto modulemap = t->modulemap_file();
        CHECK(modulemap != nullptr);
        const char* tool_type;
        std::vector<OutputFile> modulemap_outputs;
        CHECK(t->GetOutputFilesForSource(*modulemap, &tool_type,
                                         &modulemap_outputs));
        CHECK(modulemap_outputs.size() == 1u);
        ret.emplace(modulemap, t->module_name(),
                    std::move(modulemap_outputs[0]), is_self);
        break;
      }
      default:
        break;
    }
  };

  if (target->source_types_used().Get(SourceFile::SOURCE_MODULEMAP))
    add_if_new(target, true);

  for (const auto& pair : resolved.GetModuleDepsInformation(target))
    add_if_new(pair.target(), false);

  return ret;
}

void ClangModuleDep::Write(std::ostream& out,
                           const PathOutput& path_output) const {
  if (modulemap) {
    out << " -fmodule-map-file=";
    path_output.WriteFile(out, *modulemap);
  }
  if (pcm) {
    out << " -fmodule-file=" << module_name << "=";
    path_output.WriteFile(out, *pcm);
  }
}