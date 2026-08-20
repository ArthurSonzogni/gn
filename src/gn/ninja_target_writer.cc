// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_target_writer.h"

#include <algorithm>
#include <sstream>

#include "base/strings/string_util.h"
#include "gn/builtin_tool.h"
#include "gn/c_substitution_type.h"
#include "gn/config_values_extractors.h"
#include "gn/escape.h"
#include "gn/filesystem_utils.h"
#include "gn/general_tool.h"
#include "gn/ninja_action_target_writer.h"
#include "gn/ninja_binary_target_writer.h"
#include "gn/ninja_bundle_data_target_writer.h"
#include "gn/ninja_copy_target_writer.h"
#include "gn/ninja_create_bundle_target_writer.h"
#include "gn/ninja_generated_file_target_writer.h"
#include "gn/ninja_group_target_writer.h"
#include "gn/ninja_target_command_util.h"
#include "gn/ninja_utils.h"
#include "gn/output_file.h"
#include "gn/rust_substitution_type.h"
#include "gn/scheduler.h"
#include "gn/string_output_buffer.h"
#include "gn/substitution_writer.h"
#include "gn/target.h"
#include "gn/trace.h"

NinjaTargetWriter::NinjaTargetWriter(const Target* target, std::ostream& out)
    : settings_(target->settings()),
      target_(target),
      out_(out),
      path_output_(settings_->build_settings()->build_dir(),
                   settings_->build_settings()->root_path_utf8(),
                   ESCAPE_NINJA) {
  target_group_.target = target;
}

void NinjaTargetWriter::AddTargetVar(std::string_view name, std::string value) {
  target_group_.target_vars.emplace_back(name, std::move(value));
}

void NinjaTargetWriter::AddEdge(NinjaBuildEdge edge) {
  target_group_.edges.push_back(std::move(edge));
}

NinjaTargetGroup NinjaTargetWriter::GenerateTargetGroup() {
  GenerateRules();
  return std::move(target_group_);
}

void NinjaTargetWriter::SetNinjaOutputs(
    std::vector<OutputFile>* ninja_outputs) {
  ninja_outputs_ = ninja_outputs;
}

void NinjaTargetWriter::Run() {
  NinjaTargetGroup group = GenerateTargetGroup();
  if (ninja_outputs_) {
    for (const auto& edge : group.edges) {
      if (edge.is_target_output) {
        ninja_outputs_->insert(ninja_outputs_->end(), edge.outputs.begin(),
                               edge.outputs.end());
        ninja_outputs_->insert(ninja_outputs_->end(),
                               edge.implicit_outputs.begin(),
                               edge.implicit_outputs.end());
      }
    }
  }
  NinjaFile file;
  file.AddTargetGroup(std::move(group));
  file.Serialize(out_);
}

std::vector<OutputFile> NinjaTargetWriter::ToOutputFiles(
    const std::vector<SourceFile>& sources) const {
  std::vector<OutputFile> outputs;
  outputs.reserve(sources.size());
  for (const auto& source : sources) {
    outputs.emplace_back(settings_->build_settings(), source);
  }
  return outputs;
}

void NinjaTargetWriter::SetResolvedTargetData(ResolvedTargetData* resolved) {
  if (resolved) {
    resolved_owned_.reset();
    resolved_ptr_ = resolved;
  }
}

ResolvedTargetData* NinjaTargetWriter::GetResolvedTargetData() {
  return const_cast<ResolvedTargetData*>(&resolved());
}

const ResolvedTargetData& NinjaTargetWriter::resolved() const {
  if (!resolved_ptr_) {
    resolved_owned_ = std::make_unique<ResolvedTargetData>();
    resolved_ptr_ = resolved_owned_.get();
  }
  return *resolved_ptr_;
}

NinjaTargetWriter::~NinjaTargetWriter() = default;

// static
std::string NinjaTargetWriter::RunAndWriteFile(
    const Target* target,
    ResolvedTargetData* resolved,
    std::vector<OutputFile>* ninja_outputs) {
  const Settings* settings = target->settings();

  ScopedTrace trace(TraceItem::TRACE_FILE_WRITE_NINJA,
                    target->label().GetUserVisibleName(false));
  trace.SetToolchain(settings->toolchain_label());

  if (g_scheduler->verbose_logging())
    g_scheduler->Log("Computing", target->label().GetUserVisibleName(true));

  StringOutputBuffer dummy_storage;
  std::ostream dummy_rules(&dummy_storage);

  // Call out to the correct sub-type of writer. Binary targets need to be
  // written to separate files for compiler flag scoping, but other target
  // types can have their rules coalesced.
  //
  // In ninja, if a rule uses a variable (like $include_dirs) it will use
  // the value set by indenting it under the build line or it takes the value
  // from the end of the invoking scope (otherwise the current file). It does
  // not copy the value from what it was when the build line was encountered.
  // To avoid writing lots of duplicate rules for defines and cflags, etc. on
  // each source file build line, we use separate .ninja files with the shared
  // variables set at the top.
  //
  // Groups and actions don't use this type of flag, they make unique rules
  // or write variables scoped under each build line. As a result, they don't
  // need the separate files.
  NinjaTargetGroup group;
  bool needs_file_write = false;
  if (target->output_type() == Target::BUNDLE_DATA) {
    NinjaBundleDataTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    group = writer.GenerateTargetGroup();
  } else if (target->output_type() == Target::CREATE_BUNDLE) {
    NinjaCreateBundleTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    group = writer.GenerateTargetGroup();
  } else if (target->output_type() == Target::COPY_FILES) {
    NinjaCopyTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    group = writer.GenerateTargetGroup();
  } else if (target->output_type() == Target::ACTION ||
             target->output_type() == Target::ACTION_FOREACH) {
    NinjaActionTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    group = writer.GenerateTargetGroup();
  } else if (target->output_type() == Target::GROUP) {
    NinjaGroupTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    group = writer.GenerateTargetGroup();
  } else if (target->output_type() == Target::GENERATED_FILE) {
    NinjaGeneratedFileTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    group = writer.GenerateTargetGroup();
  } else if (target->IsBinary()) {
    needs_file_write = true;
    NinjaBinaryTargetWriter writer(target, dummy_rules);
    writer.SetResolvedTargetData(resolved);
    if (target->module_type().test(Target::MODULEMAP_IS_GENERATED)) {
      const SourceFile* modulemap = target->modulemap_file();
      CHECK(modulemap);

      // Write public module map
      StringOutputBuffer public_storage;
      std::ostream public_os(&public_storage);
      writer.WritePublicModuleMap(public_os, modulemap->GetDir());
      base::FilePath public_path =
          settings->build_settings()->GetFullPath(*modulemap);
      public_storage.WriteToFileIfChanged(public_path, nullptr);

      // Write private module map adjacent to the public one
      StringOutputBuffer private_storage;
      std::ostream private_os(&private_storage);
      writer.WritePrivateModuleMap(private_os, modulemap->GetDir());
      base::FilePath private_path = settings->build_settings()->GetFullPath(
          *target->private_modulemap_file());
      private_storage.WriteToFileIfChanged(private_path, nullptr);
    }
    group = writer.GenerateTargetGroup();
  } else {
    CHECK(0) << "Output type of target not handled.";
  }

  if (ninja_outputs) {
    for (const auto& edge : group.edges) {
      if (edge.is_target_output) {
        ninja_outputs->insert(ninja_outputs->end(), edge.outputs.begin(),
                              edge.outputs.end());
        ninja_outputs->insert(ninja_outputs->end(),
                              edge.implicit_outputs.begin(),
                              edge.implicit_outputs.end());
      }
    }
  }

  WritePublicInputsStampOrPhony(target, resolved, group);

  NinjaFile file;
  file.AddTargetGroup(std::move(group));

  // It's ridiculously faster to write to a string and then write that to
  // disk in one operation than to use an fstream here.
  StringOutputBuffer storage;
  file.Serialize(storage);

  if (needs_file_write) {
    // Write the ninja file.
    SourceFile ninja_file = GetNinjaFileForTarget(target);
    base::FilePath full_ninja_file =
        settings->build_settings()->GetFullPath(ninja_file);
    storage.WriteToFileIfChanged(full_ninja_file, nullptr);

    EscapeOptions options;
    options.mode = ESCAPE_NINJA;

    // Return the subninja command to load the rules file.
    std::string result = "subninja ";
    result.append(EscapeString(
        OutputFile(target->settings()->build_settings(), ninja_file).value(),
        options, nullptr));
    result.push_back('\n');
    return result;
  }

  // No separate file required, just return the rules.
  return storage.str();
}

// static
void NinjaTargetWriter::WritePublicInputsStampOrPhony(
    const Target* target,
    ResolvedTargetData* resolved,
    NinjaTargetGroup& group) {
  DCHECK(resolved);
  if (!resolved->ExportsPublicInputs(target))
    return;

  const BuildSettings* build_settings = target->settings()->build_settings();
  OutputFile output = GetPublicInputsOutputFile(target, build_settings);

  std::vector<OutputFile> deps;
  for (const auto& file : target->public_inputs()) {
    deps.emplace_back(build_settings, file);
  }
  for (const auto& dep : target->public_deps()) {
    if (resolved->ExportsPublicInputs(dep.ptr)) {
      deps.push_back(GetPublicInputsOutputFile(dep.ptr, build_settings));
    }
  }

  std::string rule;
  if (build_settings->no_stamp_files()) {
    rule = BuiltinTool::kBuiltinToolPhony;
  } else {
    rule = GetNinjaRulePrefixForToolchain(target->settings()) +
           GeneralTool::kGeneralToolStamp;
  }

  group.edges.push_back(NinjaBuildEdge{
      .rule = std::move(rule),
      .outputs = {output},
      .explicit_inputs = std::move(deps),
      .is_target_output = false,
  });
}

void NinjaTargetWriter::WriteEscapedSubstitution(const Substitution* type) {
  EscapeOptions opts;
  opts.mode = ESCAPE_NINJA;

  std::ostringstream val;
  EscapeStringToStream(
      val, SubstitutionWriter::GetTargetSubstitution(target_, type), opts);
  target_group_.target_vars.emplace_back(type->ninja_name, val.str());
}

void NinjaTargetWriter::WriteSharedVars(const SubstitutionBits& bits) {
  // Target label.
  if (bits.used.count(&SubstitutionLabel)) {
    WriteEscapedSubstitution(&SubstitutionLabel);
  }

  // Target label name.
  if (bits.used.count(&SubstitutionLabelName)) {
    WriteEscapedSubstitution(&SubstitutionLabelName);
  }

  // Target label name without toolchain.
  if (bits.used.count(&SubstitutionLabelNoToolchain)) {
    WriteEscapedSubstitution(&SubstitutionLabelNoToolchain);
  }

  // Root gen dir.
  if (bits.used.count(&SubstitutionRootGenDir)) {
    WriteEscapedSubstitution(&SubstitutionRootGenDir);
  }

  // Root out dir.
  if (bits.used.count(&SubstitutionRootOutDir)) {
    WriteEscapedSubstitution(&SubstitutionRootOutDir);
  }

  // Target gen dir.
  if (bits.used.count(&SubstitutionTargetGenDir)) {
    WriteEscapedSubstitution(&SubstitutionTargetGenDir);
  }

  // Target out dir.
  if (bits.used.count(&SubstitutionTargetOutDir)) {
    WriteEscapedSubstitution(&SubstitutionTargetOutDir);
  }

  // Target output name.
  if (bits.used.count(&SubstitutionTargetOutputName)) {
    WriteEscapedSubstitution(&SubstitutionTargetOutputName);
  }
}

void NinjaTargetWriter::WriteCCompilerVars(
    const SubstitutionBits& bits,
    bool respect_source_used,
    std::vector<NinjaVariable>& target_vars) {
  // Defines.
  if (bits.used.count(&CSubstitutionDefines)) {
    std::ostringstream val;
    RecursiveTargetConfigToStream<std::string>(kRecursiveWriterSkipDuplicates,
                                               target_, &ConfigValues::defines,
                                               DefineWriter(), val);
    target_vars.emplace_back(CSubstitutionDefines.ninja_name, val.str());
  }

  // Framework search path.
  if (bits.used.count(&CSubstitutionFrameworkDirs)) {
    const Tool* tool = target_->toolchain()->GetTool(CTool::kCToolLink);
    std::ostringstream val;
    PathOutput framework_dirs_output(
        path_output_.current_dir(),
        settings_->build_settings()->root_path_utf8(), ESCAPE_NINJA_COMMAND);
    RecursiveTargetConfigToStream<SourceDir>(
        kRecursiveWriterSkipDuplicates, target_, &ConfigValues::framework_dirs,
        FrameworkDirsWriter(framework_dirs_output,
                            tool->framework_dir_switch()),
        val);
    target_vars.emplace_back(CSubstitutionFrameworkDirs.ninja_name, val.str());
  }

  // Include directories.
  if (bits.used.count(&CSubstitutionIncludeDirs)) {
    std::ostringstream val;
    PathOutput include_path_output(
        path_output_.current_dir(),
        settings_->build_settings()->root_path_utf8(), ESCAPE_NINJA_COMMAND);
    RecursiveTargetConfigToStream<SourceDir>(
        kRecursiveWriterSkipDuplicates, target_, &ConfigValues::include_dirs,
        IncludeWriter(include_path_output), val);
    target_vars.emplace_back(CSubstitutionIncludeDirs.ninja_name, val.str());
  }

  bool has_precompiled_headers =
      target_->config_values().has_precompiled_headers();

  EscapeOptions opts;
  opts.mode = ESCAPE_NINJA_COMMAND;

  auto write_flag =
      [&](const Substitution* subst, bool has_pch, const char* tool_name,
          const std::vector<std::string>& (ConfigValues::*getter)() const) {
        if (!target_->toolchain()->substitution_bits().used.count(subst))
          return;
        std::ostringstream val;
        WriteOneFlag(kRecursiveWriterKeepDuplicates, target_, subst, has_pch,
                     tool_name, getter, opts, path_output_, val,
                     /*write_substitution=*/false, /*indent=*/false);
        target_vars.emplace_back(subst->ninja_name, val.str());
      };

  if (respect_source_used
          ? target_->source_types_used().Get(SourceFile::SOURCE_S)
          : bits.used.count(&CSubstitutionAsmFlags)) {
    write_flag(&CSubstitutionAsmFlags, false, Tool::kToolNone,
               &ConfigValues::asmflags);
  }
  if (respect_source_used
          ? (target_->source_types_used().Get(SourceFile::SOURCE_C) ||
             target_->source_types_used().Get(SourceFile::SOURCE_CPP) ||
             target_->source_types_used().Get(SourceFile::SOURCE_M) ||
             target_->source_types_used().Get(SourceFile::SOURCE_MM) ||
             target_->source_types_used().Get(SourceFile::SOURCE_MODULEMAP))
          : bits.used.count(&CSubstitutionCFlags)) {
    write_flag(&CSubstitutionCFlags, false, Tool::kToolNone,
               &ConfigValues::cflags);
  }
  if (respect_source_used
          ? target_->source_types_used().Get(SourceFile::SOURCE_C)
          : bits.used.count(&CSubstitutionCFlagsC)) {
    write_flag(&CSubstitutionCFlagsC, has_precompiled_headers, CTool::kCToolCc,
               &ConfigValues::cflags_c);
  }
  if (respect_source_used
          ? (target_->source_types_used().Get(SourceFile::SOURCE_CPP) ||
             target_->source_types_used().Get(SourceFile::SOURCE_MODULEMAP))
          : bits.used.count(&CSubstitutionCFlagsCc)) {
    write_flag(&CSubstitutionCFlagsCc, has_precompiled_headers,
               CTool::kCToolCxx, &ConfigValues::cflags_cc);
  }
  if (respect_source_used
          ? target_->source_types_used().Get(SourceFile::SOURCE_M)
          : bits.used.count(&CSubstitutionCFlagsObjC)) {
    write_flag(&CSubstitutionCFlagsObjC, has_precompiled_headers,
               CTool::kCToolObjC, &ConfigValues::cflags_objc);
  }
  if (respect_source_used
          ? target_->source_types_used().Get(SourceFile::SOURCE_MM)
          : bits.used.count(&CSubstitutionCFlagsObjCc)) {
    write_flag(&CSubstitutionCFlagsObjCc, has_precompiled_headers,
               CTool::kCToolObjCxx, &ConfigValues::cflags_objcc);
  }
  if (target_->source_types_used().SwiftSourceUsed() || !respect_source_used) {
    if (bits.used.count(&CSubstitutionSwiftModuleName)) {
      std::ostringstream val;
      EscapeStringToStream(val, target_->swift_values().module_name(), opts);
      target_vars.emplace_back(CSubstitutionSwiftModuleName.ninja_name,
                               val.str());
    }

    if (bits.used.count(&CSubstitutionSwiftBridgeHeader)) {
      std::ostringstream val;
      if (!target_->swift_values().bridge_header().is_null()) {
        path_output_.WriteFile(val, target_->swift_values().bridge_header());
      } else {
        val << R"("")";
      }
      target_vars.emplace_back(CSubstitutionSwiftBridgeHeader.ninja_name,
                               val.str());
    }

    if (bits.used.count(&CSubstitutionSwiftModuleDirs)) {
      // Uniquify the list of swiftmodule dirs (in case multiple swiftmodules
      // are generated in the same directory).
      UniqueVector<SourceDir> swiftmodule_dirs;
      for (const Target* dep : resolved().GetSwiftModuleDependencies(target_))
        swiftmodule_dirs.push_back(dep->swift_values().module_output_dir());

      std::ostringstream val;
      PathOutput swiftmodule_path_output(
          path_output_.current_dir(),
          settings_->build_settings()->root_path_utf8(), ESCAPE_NINJA_COMMAND);
      IncludeWriter swiftmodule_path_writer(swiftmodule_path_output);
      for (const SourceDir& swiftmodule_dir : swiftmodule_dirs) {
        swiftmodule_path_writer(swiftmodule_dir, val);
      }
      target_vars.emplace_back(CSubstitutionSwiftModuleDirs.ninja_name,
                               val.str());
    }
  }
}

void NinjaTargetWriter::WriteCCompilerVars(const SubstitutionBits& bits,
                                           bool respect_source_used) {
  WriteCCompilerVars(bits, respect_source_used, target_group_.target_vars);
}

void NinjaTargetWriter::WriteRustCompilerVars(
    const SubstitutionBits& bits,
    bool always_write,
    std::vector<NinjaVariable>& target_vars) {
  EscapeOptions opts;
  opts.mode = ESCAPE_NINJA_COMMAND;

  auto write_flag =
      [&](const Substitution* subst,
          const std::vector<std::string>& (ConfigValues::*getter)() const) {
        std::ostringstream val;
        WriteOneFlag(kRecursiveWriterKeepDuplicates, target_, subst, false,
                     Tool::kToolNone, getter, opts, path_output_, val,
                     /*write_substitution=*/false, /*indent=*/false);
        target_vars.emplace_back(subst->ninja_name, val.str());
      };

  if (bits.used.count(&kRustSubstitutionRustFlags) || always_write) {
    write_flag(&kRustSubstitutionRustFlags, &ConfigValues::rustflags);
  }

  if (bits.used.count(&kRustSubstitutionRustEnv) || always_write) {
    write_flag(&kRustSubstitutionRustEnv, &ConfigValues::rustenv);
  }
}

void NinjaTargetWriter::WriteRustCompilerVars(const SubstitutionBits& bits,
                                              bool always_write) {
  WriteRustCompilerVars(bits, always_write, target_group_.target_vars);
}

NinjaTargetWriter::InputDeps
NinjaTargetWriter::WriteInputDepsStampOrPhonyAndGetDep(
    const std::vector<const Target*>& additional_hard_deps,
    size_t num_output_uses) {
  CHECK(target_->toolchain()) << "Toolchain not set on target "
                              << target_->label().GetUserVisibleName(true);

  // ----------
  // Collect all input files that are input deps of this target. Knowing the
  // number before writing allows us to either skip writing the input deps
  // phony or optimize it. Use pointers to avoid copies here.
  std::vector<const SourceFile*> input_deps_sources;
  input_deps_sources.reserve(32);

  // Actions get implicit dependencies on the script itself.
  if (target_->output_type() == Target::ACTION ||
      target_->output_type() == Target::ACTION_FOREACH)
    input_deps_sources.push_back(&target_->action_values().script());

  // Input files are only considered for non-binary targets which use an
  // implicit dependency instead. The implicit dependency in this case is
  // handled separately by the binary target writer.
  if (!target_->IsBinary()) {
    for (ConfigValuesIterator iter(target_); !iter.done(); iter.Next()) {
      for (const auto& input : iter.cur().inputs())
        input_deps_sources.push_back(&input);
    }
  }

  // For an action (where we run a script only once) the sources are the same
  // as the inputs. For action_foreach, the sources will be operated on
  // separately so don't handle them here.
  if (target_->output_type() == Target::ACTION) {
    for (const auto& source : target_->sources())
      input_deps_sources.push_back(&source);
  }

  // ----------
  // Collect all target input dependencies of this target as was done for the
  // files above.
  std::vector<const Target*> input_deps_targets;
  input_deps_targets.reserve(32);

  // Hard dependencies that are direct or indirect dependencies.
  // These are large (up to 100s), hence why we check other
  const TargetSet& hard_deps = resolved().GetHardDeps(target_);
  for (const Target* target : hard_deps) {
    // BUNDLE_DATA should normally be treated as a data-only dependency
    // (see Target::IsDataOnly()). Only the CREATE_BUNDLE target, that actually
    // consumes this data, needs to have the BUNDLE_DATA as an input dependency.
    if (target->output_type() != Target::BUNDLE_DATA ||
        target_->output_type() == Target::CREATE_BUNDLE)
      input_deps_targets.push_back(target);
  }

  // Additional hard dependencies passed in. These are usually empty or small,
  // and we don't want to duplicate the explicit hard deps of the target.
  for (const Target* target : additional_hard_deps) {
    if (!hard_deps.contains(target))
      input_deps_targets.push_back(target);
  }

  // Toolchain dependencies. These must be resolved before doing anything.
  // This just writes all toolchain deps for simplicity. If we find that
  // toolchains often have more than one dependency, we could consider writing
  // a toolchain-specific phony target and only include the phony here.
  // Note that these are usually empty/small.
  std::vector<const Target*> toolchain_deps_targets;
  const LabelTargetVector& toolchain_deps = target_->toolchain()->deps();
  for (const auto& toolchain_dep : toolchain_deps) {
    // This could theoretically duplicate dependencies already in the list,
    // but it shouldn't happen in practice, is inconvenient to check for,
    // and only results in harmless redundant dependencies listed.
    toolchain_deps_targets.push_back(toolchain_dep.ptr);
  }

  // ---------
  // Write the outputs.

  if (input_deps_sources.empty() && input_deps_targets.empty() &&
      toolchain_deps_targets.empty())
    return InputDeps{};  // No input dependencies.

  InputDeps deps;
  // Inherited public_inputs target dependencies.
  std::vector<OutputFile> public_inputs_deps;
  if (!target_->public_inputs().empty()) {
    public_inputs_deps.push_back(
        GetPublicInputsOutputFile(target_, settings_->build_settings()));
  }
  for (const auto& pair : target_->public_deps()) {
    if (resolved().ExportsPublicInputs(pair.ptr)) {
      public_inputs_deps.push_back(
          GetPublicInputsOutputFile(pair.ptr, settings_->build_settings()));
    }
  }
  for (const auto& pair : target_->private_deps()) {
    if (resolved().ExportsPublicInputs(pair.ptr)) {
      public_inputs_deps.push_back(
          GetPublicInputsOutputFile(pair.ptr, settings_->build_settings()));
    }
  }
  std::sort(public_inputs_deps.begin(), public_inputs_deps.end());
  public_inputs_deps.erase(
      std::unique(public_inputs_deps.begin(), public_inputs_deps.end()),
      public_inputs_deps.end());
  deps.implicit.insert(deps.implicit.end(), public_inputs_deps.begin(),
                       public_inputs_deps.end());

  // File input deps.
  for (const SourceFile* source : input_deps_sources)
    deps.order_only.emplace_back(settings_->build_settings(), *source);
  // Target input deps. Sort by label so the output is deterministic (otherwise
  // some of the targets will have gone through std::sets which will have
  // sorted them by pointer).
  auto add_target_deps = [](std::vector<OutputFile>& deps,
                            std::vector<const Target*>& targets) {
    std::sort(targets.begin(), targets.end(),
              [](const Target* a, const Target* b) {
                return a->label() < b->label();
              });
    for (auto* dep : targets) {
      if (dep->has_dependency_output())
        deps.push_back(dep->dependency_output());
    }
  };
  add_target_deps(deps.order_only, input_deps_targets);
  add_target_deps(deps.implicit, toolchain_deps_targets);

  // If we're only generating one input dependency, or if there are no
  // dependencies, return it directly instead of writing a phony target for it.
  // Also, if there are multiple inputs, but the phony target would be
  // referenced only once, don't write it but depend on the inputs directly.
  if (deps.implicit.size() + deps.order_only.size() <= 1 ||
      num_output_uses == 1u)
    return deps;

  // Action targets are special because all of their dependencies are implicit
  // dependencies. This is because prior to
  // https://gn-review.googlesource.com/c/gn/+/22000 action targets had an
  // implicit dependency on inputdeps whereas other target types had an
  // order-only dependency on inputdeps (which at the time only had implicit
  // dependencies), and scripts may be depending on that.
  if (target_->output_type() == Target::ACTION ||
      target_->output_type() == Target::ACTION_FOREACH) {
    deps.implicit.insert(deps.implicit.end(), deps.order_only.begin(),
                         deps.order_only.end());
    deps.order_only.clear();
  }

  OutputFile input_stamp_or_phony;
  std::string tool;
  if (settings_->build_settings()->no_stamp_files()) {
    // Make a phony target. We don't need to worry about an empty phony target,
    // as we would return early if there were no inputs.
    input_stamp_or_phony = GetOutputFile(*target_, BuildDirType::PHONY,
                                         target_->label().name(), ".inputdeps");
    tool = BuiltinTool::kBuiltinToolPhony;
  } else {
    // Make a stamp file.
    input_stamp_or_phony =
        GetOutputFile(*target_, BuildDirType::OBJ, target_->label().name(),
                      ".inputdeps.stamp");

    tool = GetNinjaRulePrefixForToolchain(settings_) +
           GeneralTool::kGeneralToolStamp;
  }

  // These are not real outputs, so do not mark as target output.
  // See https://gn.issues.chromium.org/448860851.
  AddEdge(NinjaBuildEdge{
      .rule = tool,
      .outputs = {input_stamp_or_phony},
      .explicit_inputs = deps.implicit,
      .order_only_inputs = deps.order_only,
      .is_target_output = false,
  });

  InputDeps result;
  result.implicit.push_back(input_stamp_or_phony);
  return result;
}

void NinjaTargetWriter::WriteStampOrPhonyForTarget(
    const std::vector<OutputFile>& files,
    const std::vector<OutputFile>& order_only_deps) {
  // Add dependency outputs of public_deps to this target's stamp or phony
  // target to propagate public dependencies transitively. This allows
  // dependents of this target to implicitly depend on the public_deps without
  // listing them all directly on their build lines, preventing inflation of
  // implicit inputs.
  std::vector<OutputFile> all_files = files;
  for (const auto& pair : target_->public_deps()) {
    if (pair.ptr->has_dependency_output() && !pair.ptr->IsDataOnly()) {
      OutputFile dep_out = pair.ptr->dependency_output();
      if (std::find(all_files.begin(), all_files.end(), dep_out) ==
          all_files.end()) {
        all_files.push_back(dep_out);
      }
    }
  }

  OutputFile output_file;
  std::string rule;
  // We should have already discerned whether this target is a stamp or a phony.
  // If there's a dependency_output_file, it should be a stamp. Else is a phony
  // or omitted phony (in which case, we don't write it).
  if (target_->has_dependency_output_file()) {
    // Make a stamp target.
    output_file = target_->dependency_output_file();

    // First validate that the target's dependency is a stamp file. Otherwise,
    // we shouldn't have gotten here!
    CHECK(base::EndsWithCaseInsensitiveASCII(output_file.value(), ".stamp"))
        << "Output should end in \".stamp\" for stamp file output. Instead "
           "got: "
        << "\"" << output_file.value() << "\"";

    rule = GetNinjaRulePrefixForToolchain(settings_) +
           GeneralTool::kGeneralToolStamp;
  } else if (target_->has_dependency_output_alias()) {
    // Make a phony target.
    output_file = target_->dependency_output_alias();
    CHECK(!output_file.value().empty());

    rule = BuiltinTool::kBuiltinToolPhony;
  } else {
    // This is the omitted phony case. We should not get here if there were any
    // dependencies, so ensure that none got added.
    CHECK(all_files.empty());
    CHECK(order_only_deps.empty());
    return;
  }

  NinjaBuildEdge edge{
      .rule = std::move(rule),
      .outputs = {output_file},
      .explicit_inputs = std::move(all_files),
      .order_only_inputs = order_only_deps,
  };
  AddValidationInputs(edge);
  AddEdge(std::move(edge));
}

void NinjaTargetWriter::AddValidationInputs(NinjaBuildEdge& edge) const {
  for (const auto& pair : target_->validations()) {
    // This check is needed because empty groups have no output.
    if (pair.ptr->has_dependency_output()) {
      edge.validation_inputs.push_back(pair.ptr->dependency_output());
    }
  }
}
