// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_NINJA_FILE_H_
#define TOOLS_GN_NINJA_FILE_H_

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "gn/output_file.h"

class StringOutputBuffer;
class Target;

// A single variable assignment in Ninja syntax (e.g. cflags = -fPIC).
struct NinjaVariable {
  std::string_view name;
  std::string value;

  bool operator==(const NinjaVariable& other) const = default;
};

// Represents a single 'build <outputs>: <rule> <inputs>' edge.
struct NinjaBuildEdge {
  std::string rule;
  std::vector<OutputFile> outputs;
  std::vector<OutputFile> implicit_outputs = {};

  std::vector<OutputFile> explicit_inputs = {};
  std::vector<OutputFile> implicit_inputs = {};
  std::vector<OutputFile> order_only_inputs = {};
  std::vector<OutputFile> validation_inputs = {};

  // Variables specific to this edge (e.g. source_file_part, source_name_part).
  std::vector<NinjaVariable> edge_vars = {};

  // Whether the outputs of this edge are target outputs for
  // --ide=ninja_outputs. Intermediate input dependency stamps/phonies are not
  // target outputs. See https://gn.issues.chromium.org/448860851.
  bool is_target_output = true;
};

// Represents a group of build edges and shared variables for a target.
struct NinjaTargetGroup {
  const Target* target = nullptr;

  // Custom rule definitions specific to this target (e.g. for action targets).
  std::vector<std::string> custom_rules;

  // Variables shared across all edges in this target (e.g. cflags, defines,
  // target_out_dir).
  std::vector<NinjaVariable> target_vars;

  // Compilation, link, or action build edges for this target.
  std::vector<NinjaBuildEdge> edges;
};

// Represents a complete Ninja build file.
class NinjaFile {
 public:
  // Top-level / hoisted variables.
  std::vector<NinjaVariable> file_vars;

  // Custom rule definitions.
  std::vector<std::string> custom_rules;

  // Target groups contained in this file.
  std::vector<NinjaTargetGroup> targets;

  // Adds a target group to this file.
  void AddTargetGroup(NinjaTargetGroup group);

  // Serializes the Ninja AST to an output stream or StringOutputBuffer.
  void Serialize(std::ostream& out);
  void Serialize(StringOutputBuffer& out);

 private:
  // Hoists identical target variables across all targets into file_vars.
  void Hoist();
};

#endif  // TOOLS_GN_NINJA_FILE_H_
