// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/edit_subcommands.h"

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "gn/build_file_editor.h"
#include "gn/err.h"
#include "gn/location.h"
#include "gn/parse_tree.h"
#include "gn/value.h"

namespace {

// Parses a single string argument into a primitive GN Value (bool, int, or
// string).
Result<Value> ParseValue(std::string_view val_string) {
  if (val_string == "true") {
    return Value(nullptr, true);
  }
  if (val_string == "false") {
    return Value(nullptr, false);
  }

  int64_t result_int;
  if (base::StringToInt64(val_string, &result_int)) {
    return Value(nullptr, result_int);
  }

  return Value(nullptr, std::string(val_string));
}

// Parses multiple string arguments into a vector of GN Values.
Result<std::vector<Value>> ParseValues(base::span<const std::string> values) {
  std::vector<Value> list_elements;
  list_elements.reserve(values.size());
  for (const std::string& val_str : values) {
    ASSIGN_OR_RETURN(Value val, ParseValue(val_str));
    list_elements.push_back(std::move(val));
  }
  return list_elements;
}

const TreeNode* FirstAssignment(const std::vector<TreeNode>& assignments) {
  for (const auto& assignment : assignments) {
    if (!assignment.is_conditional() && !assignment.is_modification())
      return &assignment;
  }
  return nullptr;
}

// Helper to create an EditCommand that loops over all matched targets in a
// BuildFile.
EditCommand EditTargetCommand(
    std::function<Err(BuildFile&, const EditTarget&, EditState&)>
        apply_to_target) {
  return [apply_to_target = std::move(apply_to_target)](
             BuildFile& build_file, EditState& state) -> Err {
    for (const auto& target : build_file.targets()) {
      RETURN_IF_ERROR(apply_to_target(build_file, target, state));
    }
    return Ok();
  };
}

bool RemoveFromTarget(const EditTarget& target,
                      const std::string& attribute,
                      const Value& value,
                      EditState& state) {
  bool done = false;
  for (auto& assignment : target.assignments(attribute)) {
    auto matches = FindListElementInAssignment(target, assignment, value);

    for (const auto& match : matches) {
      match.RemoveSelf(state, target);
    }
    done |= !matches.empty();
  }

  if (!done && target.is_explicit) {
    target.add_warning(state, "does not contain the value " +
                                  value.ToString(true) + " in attribute \"" +
                                  attribute + "\".");
  }
  return done;
}

EditCommand DeleteCommand() {
  return EditTargetCommand([](BuildFile& build_file, const EditTarget& target,
                              EditState& state) -> Err {
    target.node.RemoveSelf(state, target);
    return Ok();
  });
}

EditCommand RemoveAttributeCommand(std::string attribute) {
  return EditTargetCommand([attribute = std::move(attribute)](
                               BuildFile& build_file, const EditTarget& target,
                               EditState& state) -> Err {
    auto assignments = target.assignments(attribute);
    for (auto& assignment : assignments) {
      assignment.RemoveSelf(state, target);
    }
    if (assignments.empty() && target.is_explicit) {
      target.add_warning(
          state, "does not contain the attribute \"" + attribute + "\".");
    }
    return Ok();
  });
}

EditCommand RemoveFromAttributeCommand(std::string attribute,
                                       std::vector<Value> values) {
  return EditTargetCommand(
      [attribute = std::move(attribute), values = std::move(values)](
          BuildFile& build_file, const EditTarget& target,
          EditState& state) -> Err {
        for (const auto& value : values) {
          RemoveFromTarget(target, attribute, value, state);
        }
        return Ok();
      });
}

// Sets an attribute to a value.
EditCommand SetCommand(std::string attribute, Value value) {
  return EditTargetCommand([=](BuildFile& build_file, const EditTarget& target,
                               EditState& state) -> Err {
    auto assignments = target.assignments(attribute);
    const auto* first = FirstAssignment(assignments);
    for (const auto& assignment : assignments) {
      if (&assignment != first) {
        assignment.RemoveSelf(state, target);
      }
    }

    if (first) {
      (*first)->AsBinaryOpMut()->set_right(build_file.to_node(value));
    } else {
      target.block->append_statement(
          build_file.create_assignment(attribute, build_file.to_node(value)));
    }

    return Ok();
  });
}

}  // namespace

Result<EditCommand> ParseCommand(std::vector<std::string> args) {
  if (args.empty()) {
    return Err(Location(), "Empty command.");
  }

  if (args[0] == "delete") {
    if (args.size() != 1) {
      return Err(Location(), "Invalid delete command.", "Usage: delete");
    }
    return DeleteCommand();
  } else if (args[0] == "remove") {
    if (args.size() < 2) {
      return Err(Location(), "Invalid remove command.",
                 "Usage: remove <attribute> [<value(s)>]");
    } else if (args.size() == 2) {
      return RemoveAttributeCommand(args[1]);
    }
    ASSIGN_OR_RETURN(std::vector<Value> values,
                     ParseValues(base::make_span(args).subspan(2)));
    return RemoveFromAttributeCommand(args[1], std::move(values));
  } else if (args[0] == "set") {
    if (args.size() < 3) {
      return Err(Location(),
                 "Invalid set command: missing attribute or value.\n"
                 "Usage: set <attribute> <value...>");
    }

    std::string_view attribute = args[1];
    bool force_list = false;
    constexpr std::string_view kListSuffix = ":list";
    if (attribute.ends_with(kListSuffix)) {
      attribute.remove_suffix(kListSuffix.size());
      force_list = true;
    }

    auto value_args = base::make_span(args).subspan(2);
    Value val;
    if (value_args.size() > 1 || force_list) {
      ASSIGN_OR_RETURN(std::vector<Value> list_elements,
                       ParseValues(value_args));
      val = Value(nullptr, std::move(list_elements));
    } else {
      ASSIGN_OR_RETURN(val, ParseValue(value_args[0]));
    }

    return SetCommand(std::string(attribute), std::move(val));
  } else {
    return Err(Location(),
               "Unknown edit command: " + std::string(args[0]) +
                   "\n"
                   "See `gn help edit` for list of supported commands.");
  }
}
