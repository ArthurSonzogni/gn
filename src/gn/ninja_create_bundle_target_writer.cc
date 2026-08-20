// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_create_bundle_target_writer.h"

#include <iterator>

#include "base/strings/string_util.h"
#include "gn/builtin_tool.h"
#include "gn/filesystem_utils.h"
#include "gn/general_tool.h"
#include "gn/ninja_utils.h"
#include "gn/output_file.h"
#include "gn/scheduler.h"
#include "gn/string_output_buffer.h"
#include "gn/substitution_writer.h"
#include "gn/target.h"
#include "gn/toolchain.h"

namespace {

bool TargetRequireAssetCatalogCompilation(const Target* target) {
  return !target->bundle_data().assets_catalog_sources().empty() ||
         !target->bundle_data().partial_info_plist().is_null();
}

void FailWithMissingToolError(const char* tool_name, const Target* target) {
  g_scheduler->FailWithError(
      Err(nullptr, std::string(tool_name) + " tool not defined",
          "The toolchain " +
              target->toolchain()->label().GetUserVisibleName(false) +
              "\n"
              "used by target " +
              target->label().GetUserVisibleName(false) +
              "\n"
              "doesn't define a \"" +
              tool_name + "\" tool."));
}

bool EnsureAllToolsAvailable(const Target* target) {
  const char* kRequiredTools[] = {
      GeneralTool::kGeneralToolCopyBundleData,
      GeneralTool::kGeneralToolStamp,
  };

  for (size_t i = 0; i < std::size(kRequiredTools); ++i) {
    if (!target->toolchain()->GetTool(kRequiredTools[i])) {
      FailWithMissingToolError(kRequiredTools[i], target);
      return false;
    }
  }

  // The compile_xcassets tool is only required if the target has asset
  // catalog resources to compile.
  if (TargetRequireAssetCatalogCompilation(target)) {
    if (!target->toolchain()->GetTool(
            GeneralTool::kGeneralToolCompileXCAssets)) {
      FailWithMissingToolError(GeneralTool::kGeneralToolCompileXCAssets,
                               target);
      return false;
    }
  }

  return true;
}

}  // namespace

NinjaCreateBundleTargetWriter::NinjaCreateBundleTargetWriter(
    const Target* target,
    std::ostream& out)
    : NinjaTargetWriter(target, out) {}

NinjaCreateBundleTargetWriter::~NinjaCreateBundleTargetWriter() = default;

void NinjaCreateBundleTargetWriter::GenerateRules() {
  if (!EnsureAllToolsAvailable(target_))
    return;

  // Stamp users are CopyBundleData, CompileAssetsCatalog, PostProcessing and
  // StampForTarget.
  size_t num_stamp_uses = 4;
  NinjaTargetWriter::InputDeps stamp_deps = WriteInputDepsStampOrPhonyAndGetDep(
      std::vector<const Target*>(), num_stamp_uses);
  std::vector<OutputFile> implicit_deps = stamp_deps.implicit;
  std::vector<OutputFile> order_only_deps = stamp_deps.order_only;

  std::string post_processing_rule_name = WritePostProcessingRuleDefinition();

  std::vector<OutputFile> output_files;
  WriteCopyBundleDataSteps(implicit_deps, order_only_deps, &output_files);
  WriteCompileAssetsCatalogStep(implicit_deps, order_only_deps, &output_files);
  WritePostProcessingStep(post_processing_rule_name, implicit_deps,
                          order_only_deps, &output_files);

  for (const Target* data_dep : resolved().GetDataDeps(target_)) {
    if (data_dep->has_dependency_output())
      order_only_deps.push_back(data_dep->dependency_output());
  }

  // If the target does not have a phony target to write, then we have nothing
  // left to do.
  if (!target_->has_dependency_output())
    return;

  WriteStampOrPhonyForTarget(output_files, order_only_deps);

  // Write a phony target for the outer bundle directory. This allows other
  // targets to treat the entire bundle as a single unit, even though it is
  // a directory, so that it can be depended upon as a discrete build edge.
  AddEdge(NinjaBuildEdge{
      .rule = BuiltinTool::kBuiltinToolPhony,
      .outputs = {OutputFile(
          settings_->build_settings(),
          target_->bundle_data().GetBundleRootDirOutput(settings_))},
      .explicit_inputs = {target_->dependency_output()},
  });
}

std::string NinjaCreateBundleTargetWriter::WritePostProcessingRuleDefinition() {
  if (target_->bundle_data().post_processing_script().is_null())
    return std::string();

  std::string target_label = target_->label().GetUserVisibleName(true);
  std::string custom_rule_name(target_label);
  base::ReplaceChars(custom_rule_name, ":/()", "_", &custom_rule_name);
  custom_rule_name.append("_post_processing_rule");

  std::ostringstream rule_out;
  rule_out << "rule " << custom_rule_name << std::endl;
  rule_out << "  command = ";
  path_output_.WriteFile(rule_out, settings_->build_settings()->python_path());
  rule_out << " ";
  path_output_.WriteFile(rule_out,
                         target_->bundle_data().post_processing_script());

  const SubstitutionList& args = target_->bundle_data().post_processing_args();
  EscapeOptions args_escape_options;
  args_escape_options.mode = ESCAPE_NINJA_COMMAND;

  for (const auto& arg : args.list()) {
    rule_out << " ";
    SubstitutionWriter::WriteWithNinjaVariables(arg, args_escape_options,
                                                rule_out);
  }
  rule_out << std::endl;
  rule_out << "  description = POST PROCESSING " << target_label << std::endl;
  rule_out << "  restat = 1" << std::endl;

  target_group_.custom_rules.push_back(rule_out.str());

  WritePostProcessingManifestFile();
  return custom_rule_name;
}

void NinjaCreateBundleTargetWriter::WritePostProcessingManifestFile() {
  const BundleData& bundle_data = target_->bundle_data();
  const SourceFile& manifest_path = bundle_data.post_processing_manifest();
  if (manifest_path.is_null()) {
    return;
  }

  const BuildSettings* build_settings = settings_->build_settings();
  const base::FilePath bundle_root_dir = build_settings->GetFullPath(
      bundle_data.GetBundleRootDirOutputAsDir(settings_));

  StringOutputBuffer storage;
  std::ostream manifest(&storage);

  std::vector<SourceFile> outputs;
  bundle_data.GetOutputsAsSourceFiles(settings_, target_, &outputs, nullptr);
  for (const SourceFile& output_file : outputs) {
    const base::FilePath full_path = build_settings->GetFullPath(output_file);

    base::FilePath relative_path;
    if (bundle_root_dir.AppendRelativePath(full_path, &relative_path)) {
      manifest << relative_path.As8Bit() << "\n";
    }
  }

  storage.WriteToFileIfChanged(build_settings->GetFullPath(manifest_path),
                               nullptr);
}

void NinjaCreateBundleTargetWriter::WriteCopyBundleDataSteps(
    const std::vector<OutputFile>& implicit_deps,
    const std::vector<OutputFile>& order_only_deps,
    std::vector<OutputFile>* output_files) {
  for (const BundleFileRule& file_rule : target_->bundle_data().file_rules())
    WriteCopyBundleFileRuleSteps(file_rule, implicit_deps, order_only_deps,
                                 output_files);
}

void NinjaCreateBundleTargetWriter::WriteCopyBundleFileRuleSteps(
    const BundleFileRule& file_rule,
    const std::vector<OutputFile>& implicit_deps,
    const std::vector<OutputFile>& order_only_deps,
    std::vector<OutputFile>* output_files) {
  // Note that we don't write implicit deps for copy steps. "copy_bundle_data"
  // steps as this is most likely implemented using hardlink in the common case.
  // See NinjaCopyTargetWriter::WriteCopyRules() for a detailed explanation.
  for (const SourceFile& source_file : file_rule.sources()) {
    // There is no need to check for errors here as the substitution will have
    // been performed when computing the list of output of the target during
    // the Target::OnResolved phase earlier.
    OutputFile expanded_output_file;
    file_rule.ApplyPatternToSourceAsOutputFile(
        settings_, target_, target_->bundle_data(), source_file,
        &expanded_output_file,
        /*err=*/nullptr);
    output_files->push_back(expanded_output_file);

    AddEdge(NinjaBuildEdge{
        .rule = GetNinjaRulePrefixForToolchain(settings_) +
                GeneralTool::kGeneralToolCopyBundleData,
        .outputs = {expanded_output_file},
        .explicit_inputs = {OutputFile(settings_->build_settings(),
                                       source_file)},
        .implicit_inputs = implicit_deps,
        .order_only_inputs = order_only_deps,
    });
  }
}

void NinjaCreateBundleTargetWriter::WriteCompileAssetsCatalogStep(
    const std::vector<OutputFile>& implicit_deps,
    const std::vector<OutputFile>& order_only_deps,
    std::vector<OutputFile>* output_files) {
  if (!TargetRequireAssetCatalogCompilation(target_))
    return;

  OutputFile compiled_catalog;
  if (!target_->bundle_data().assets_catalog_sources().empty()) {
    compiled_catalog =
        OutputFile(settings_->build_settings(),
                   target_->bundle_data().GetCompiledAssetCatalogPath());
    output_files->push_back(compiled_catalog);
  }

  OutputFile partial_info_plist;
  if (!target_->bundle_data().partial_info_plist().is_null()) {
    partial_info_plist =
        OutputFile(settings_->build_settings(),
                   target_->bundle_data().partial_info_plist());

    output_files->push_back(partial_info_plist);
  }

  // If there are no asset catalog to compile but the "partial_info_plist" is
  // non-empty, then add a target to generate an empty file (to avoid breaking
  // code that depends on this file existence).
  if (target_->bundle_data().assets_catalog_sources().empty()) {
    DCHECK(!target_->bundle_data().partial_info_plist().is_null());

    AddEdge(NinjaBuildEdge{
        .rule = GetNinjaRulePrefixForToolchain(settings_) +
                GeneralTool::kGeneralToolStamp,
        .outputs = {partial_info_plist},
        .implicit_inputs = implicit_deps,
        .order_only_inputs = order_only_deps,
    });
    return;
  }

  OutputFile input_dep = WriteCompileAssetsCatalogInputDepsStampOrPhony(
      target_->bundle_data().assets_catalog_deps());

  std::vector<OutputFile> implicit_outputs;
  if (partial_info_plist != OutputFile()) {
    // If "partial_info_plist" is non-empty, then add it to list of implicit
    // outputs of the asset catalog compilation, so that target can use it
    // without getting the ninja error "'foo', needed by 'bar', missing and
    // no known rule to make it".
    implicit_outputs.push_back(partial_info_plist);
  }

  std::vector<OutputFile> implicit_inputs;
  implicit_inputs.push_back(input_dep);
  implicit_inputs.insert(implicit_inputs.end(), implicit_deps.begin(),
                         implicit_deps.end());

  std::vector<NinjaVariable> edge_vars;
  edge_vars.emplace_back("product_type", target_->bundle_data().product_type());

  if (partial_info_plist != OutputFile()) {
    std::ostringstream ss;
    path_output_.WriteFile(ss, partial_info_plist);
    edge_vars.emplace_back("partial_info_plist", ss.str());
  }

  const std::vector<SubstitutionPattern>& flags =
      target_->bundle_data().xcasset_compiler_flags().list();
  if (!flags.empty()) {
    std::ostringstream ss;
    EscapeOptions args_escape_options;
    args_escape_options.mode = ESCAPE_NINJA_COMMAND;
    for (const auto& flag : flags) {
      ss << " ";
      SubstitutionWriter::WriteWithNinjaVariables(flag, args_escape_options,
                                                  ss);
    }
    edge_vars.emplace_back(SubstitutionXcassetsCompilerFlags.ninja_name,
                           ss.str());
  }

  AddEdge(NinjaBuildEdge{
      .rule = GetNinjaRulePrefixForToolchain(settings_) +
              GeneralTool::kGeneralToolCompileXCAssets,
      .outputs = {compiled_catalog},
      .implicit_outputs = std::move(implicit_outputs),
      .explicit_inputs =
          ToOutputFiles(target_->bundle_data().assets_catalog_sources()),
      .implicit_inputs = std::move(implicit_inputs),
      .order_only_inputs = order_only_deps,
      .edge_vars = std::move(edge_vars),
  });
}

OutputFile
NinjaCreateBundleTargetWriter::WriteCompileAssetsCatalogInputDepsStampOrPhony(
    const std::vector<const Target*>& dependencies) {
  DCHECK(!dependencies.empty());
  if (dependencies.size() == 1) {
    return dependencies[0]->has_dependency_output()
               ? dependencies[0]->dependency_output()
               : OutputFile{};
  }

  OutputFile xcassets_input_stamp_or_phony;
  std::string tool;
  if (settings_->build_settings()->no_stamp_files()) {
    xcassets_input_stamp_or_phony =
        GetOutputFile(*target_, BuildDirType::PHONY, target_->label().name(),
                      ".xcassets.inputdeps");
    tool = BuiltinTool::kBuiltinToolPhony;
  } else {
    xcassets_input_stamp_or_phony =
        GetOutputFile(*target_, BuildDirType::OBJ, target_->label().name(),
                      ".xcassets.inputdeps.stamp");
    tool = GetNinjaRulePrefixForToolchain(settings_) +
           GeneralTool::kGeneralToolStamp;
  }

  std::vector<OutputFile> explicit_inputs;
  for (const Target* target : dependencies) {
    if (target->has_dependency_output()) {
      explicit_inputs.push_back(target->dependency_output());
    }
  }

  AddEdge(NinjaBuildEdge{
      .rule = tool,
      .outputs = {xcassets_input_stamp_or_phony},
      .explicit_inputs = std::move(explicit_inputs),
  });
  return xcassets_input_stamp_or_phony;
}

void NinjaCreateBundleTargetWriter::WritePostProcessingStep(
    const std::string& post_processing_rule_name,
    const std::vector<OutputFile>& implicit_deps,
    const std::vector<OutputFile>& order_only_deps,
    std::vector<OutputFile>* output_files) {
  if (post_processing_rule_name.empty())
    return;

  OutputFile post_processing_input_stamp_file =
      WritePostProcessingInputDepsStampOrPhony(implicit_deps, order_only_deps,
                                               output_files);
  DCHECK(!post_processing_input_stamp_file.value().empty());

  std::vector<OutputFile> post_processing_output_files;
  SubstitutionWriter::GetListAsOutputFiles(
      settings_, target_->bundle_data().post_processing_outputs(),
      &post_processing_output_files);

  // Since the post-processing step depends on all the files from the bundle,
  // the create_bundle stamp can just depends on the output of the signature
  // script (dependencies are transitive).
  *output_files = post_processing_output_files;

  AddEdge(NinjaBuildEdge{
      .rule = post_processing_rule_name,
      .outputs = std::move(post_processing_output_files),
      .implicit_inputs = {post_processing_input_stamp_file},
  });
}

OutputFile
NinjaCreateBundleTargetWriter::WritePostProcessingInputDepsStampOrPhony(
    const std::vector<OutputFile>& implicit_deps,
    const std::vector<OutputFile>& order_only_deps,
    std::vector<OutputFile>* output_files) {
  std::vector<SourceFile> post_processing_input_files;
  post_processing_input_files.push_back(
      target_->bundle_data().post_processing_script());
  post_processing_input_files.insert(
      post_processing_input_files.end(),
      target_->bundle_data().post_processing_sources().begin(),
      target_->bundle_data().post_processing_sources().end());
  for (const OutputFile& output_file : *output_files) {
    post_processing_input_files.push_back(
        output_file.AsSourceFile(settings_->build_settings()));
  }

  DCHECK(!post_processing_input_files.empty());
  if (post_processing_input_files.size() == 1 && implicit_deps.empty() &&
      order_only_deps.empty())
    return OutputFile(settings_->build_settings(),
                      post_processing_input_files[0]);

  OutputFile stamp_or_phony;
  std::string tool;
  if (settings_->build_settings()->no_stamp_files()) {
    // Make a phony target. We don't need to worry about an empty phony target,
    // as those would have been peeled off already.
    stamp_or_phony =
        GetOutputFile(*target_, BuildDirType::PHONY, target_->label().name(),
                      ".postprocessing.inputdeps");
    tool = BuiltinTool::kBuiltinToolPhony;
  } else {
    // Make a stamp target.
    stamp_or_phony =
        GetOutputFile(*target_, BuildDirType::OBJ, target_->label().name(),
                      ".postprocessing.inputdeps.stamp");
    tool = GetNinjaRulePrefixForToolchain(settings_) +
           GeneralTool::kGeneralToolStamp;
  }

  AddEdge(NinjaBuildEdge{
      .rule = tool,
      .outputs = {stamp_or_phony},
      .explicit_inputs = ToOutputFiles(post_processing_input_files),
      .implicit_inputs = implicit_deps,
      .order_only_inputs = order_only_deps,
  });
  return stamp_or_phony;
}
