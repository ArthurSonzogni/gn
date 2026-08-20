// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_file.h"

#include <ostream>
#include <string>

#include "base/logging.h"
#include "gn/escape.h"
#include "gn/string_output_buffer.h"

namespace {

void WriteOutputFile(std::ostream& out, const OutputFile& file) {
  EscapeOptions opts;
  opts.mode = ESCAPE_NINJA;
  out << " ";
  EscapeStringToStream(out, file.value(), opts);
}

void WriteVariable(std::ostream& out,
                   std::string_view name,
                   const std::string& value,
                   bool indent = false) {
  if (indent)
    out << "  ";
  out << name << " =";
  if (!value.empty()) {
    if (!value.starts_with(' '))
      out << " ";
    out << value;
  }
  out << '\n';
}

void WriteRules(std::ostream& out, const std::vector<std::string>& rules) {
  if (!rules.empty()) {
    for (const auto& rule : rules) {
      out << rule << '\n';
    }
    out << '\n';
  }
}

}  // namespace

void NinjaFile::AddTargetGroup(NinjaTargetGroup group) {
  targets.push_back(std::move(group));
}

void NinjaFile::Hoist() {
  if (targets.empty())
    return;

  if (targets.size() == 1) {
    file_vars = std::move(targets.front().target_vars);
    targets.front().target_vars.clear();
    return;
  }

  CHECK(false) << "Multiple targets per ninja file not yet implemented";
}

void NinjaFile::Serialize(std::ostream& out) {
  Hoist();

  for (const auto& var : file_vars) {
    WriteVariable(out, var.name, var.value, /*indent=*/false);
  }
  if (!file_vars.empty())
    out << "\n\n";

  WriteRules(out, custom_rules);

  for (size_t i = 0; i < targets.size(); ++i) {
    if (i > 0)
      out << "\n\n";
    const auto& target = targets[i];
    WriteRules(out, target.custom_rules);
    for (size_t e = 0; e < target.edges.size(); ++e) {
      if (e > 0)
        out << "\n";
      const auto& edge = target.edges[e];
      out << "build";
      for (const auto& output : edge.outputs)
        WriteOutputFile(out, output);
      if (!edge.implicit_outputs.empty()) {
        out << " |";
        for (const auto& out_file : edge.implicit_outputs)
          WriteOutputFile(out, out_file);
      }
      out << ": " << edge.rule;
      for (const auto& input : edge.explicit_inputs)
        WriteOutputFile(out, input);
      if (!edge.implicit_inputs.empty()) {
        out << " |";
        for (const auto& in_file : edge.implicit_inputs)
          WriteOutputFile(out, in_file);
      }
      if (!edge.order_only_inputs.empty()) {
        out << " ||";
        for (const auto& in_file : edge.order_only_inputs)
          WriteOutputFile(out, in_file);
      }
      if (!edge.validation_inputs.empty()) {
        out << " |@";
        for (const auto& in_file : edge.validation_inputs)
          WriteOutputFile(out, in_file);
      }
      out << "\n";

      for (const auto& var : target.target_vars) {
        WriteVariable(out, var.name, var.value, /*indent=*/true);
      }
      for (const auto& var : edge.edge_vars) {
        WriteVariable(out, var.name, var.value, /*indent=*/true);
      }
    }
  }
}

void NinjaFile::Serialize(StringOutputBuffer& out) {
  std::ostream os(&out);
  Serialize(os);
}
