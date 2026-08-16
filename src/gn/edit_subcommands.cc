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

const TreeNode* FirstUnconditionalAssignment(
    const std::vector<TreeNode>& assignments) {
  for (const auto& assignment : assignments) {
    if (!assignment.is_conditional() && assignment.AsAssignment())
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

void AddToTarget(BuildFile& build_file,
                 const EditTarget& target,
                 const std::string& attribute,
                 const std::vector<Value>& values) {
  auto assignments = target.assignments(attribute);
  std::vector<Value> to_add = values;

  // Iterate over a copy of values since we're mutating it.
  for (const auto& value : values) {
    for (auto& assignment : assignments) {
      auto matches = FindListElementInAssignment(target, assignment, value);
      for (const auto& match : matches) {
        if (assignment.is_conditional()) {
          // If it's assigned conditionally, remove it from the list first,
          // since we're going to assign it unconditionally.
          // Unlike usual we don't mark this with a comment, because this is
          // safe.
          match.RemoveSelfUnconditionally();
        } else {
          // If it's added unconditionally, we don't need to worry about
          // adding it anymore.
          std::erase(to_add, value);
        }
      }
    }
  }

  if (const auto* first = FirstUnconditionalAssignment(assignments); first) {
    // Case A: There exists an unconditional assignment -> add values to it.
    ListNode* target_list = nullptr;
    if (auto list = FindListInAssignment(*first)) {
      // The expression is something like `[ "a" ]` or `foo + [ "a" ]`
      // In this case we just add directly to the first list "literal" we
      // find.
      target_list = *list;
    } else {
      // The expression doesn't have a list literal (eg. `foo`)
      // Rewrite it as `[] + foo` so we can add to the empty list.
      auto* op = first->AsAssignment();
      auto empty_list_val =
          build_file.to_node(Value(nullptr, std::vector<Value>{}));
      target_list = empty_list_val->AsListMut();

      auto plus_node = std::make_unique<BinaryOpNode>();
      plus_node->set_op(Token(build_file.location(), Token::PLUS, "+"));
      plus_node->set_left(std::move(empty_list_val));
      plus_node->set_right(op->take_right());

      op->set_right(std::move(plus_node));
    }

    for (const auto& value : to_add) {
      target_list->append_item(build_file.to_node(value));
    }
  } else if (!assignments.empty()) {
    // Case B: attr is only defined conditionally -> add attr = [value] at the
    // start of the block, change all other assignments to "+=".
    for (auto& assignment : assignments) {
      if (auto* op = assignment->AsBinaryOpMut()) {
        if (op->op().type() == Token::EQUAL) {
          op->set_op(Token(op->op().location(), Token::PLUS_EQUALS, "+="));
        }
      }
    }
    target.block->statements().insert(
        target.block->statements().begin(),
        build_file.create_assignment(
            attribute,
            build_file.to_node(Value(nullptr, std::vector<Value>(to_add)))));
  } else {
    // Case C: attr is not defined -> add attr = [value] at the end of the
    // block.
    target.block->append_statement(build_file.create_assignment(
        attribute,
        build_file.to_node(Value(nullptr, std::vector<Value>(to_add)))));
  }
}

EditCommand AddToAttributeCommand(std::string attribute,
                                  std::vector<Value> values) {
  return EditTargetCommand(
      [attribute = std::move(attribute), values = std::move(values)](
          BuildFile& build_file, const EditTarget& target,
          EditState& state) -> Err {
        AddToTarget(build_file, target, attribute, values);
        return Ok();
      });
}
EditCommand DeleteCommand() {
  return EditTargetCommand([](BuildFile& build_file, const EditTarget& target,
                              EditState& state) -> Err {
    target.node.RemoveSelf(state, target);
    return Ok();
  });
}

EditCommand MoveCommand(std::string from_attribute,
                        std::string to_attribute,
                        std::vector<Value> values) {
  return EditTargetCommand([from_attribute = std::move(from_attribute),
                            to_attribute = std::move(to_attribute),
                            values = std::move(values)](
                               BuildFile& build_file, const EditTarget& target,
                               EditState& state) -> Err {
    std::vector<Value> moved_values;
    for (const auto& value : values) {
      if (RemoveFromTarget(target, from_attribute, value, state)) {
        moved_values.push_back(value);
      }
    }
    if (!moved_values.empty()) {
      AddToTarget(build_file, target, to_attribute, moved_values);
    }
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
    const auto* first = FirstUnconditionalAssignment(assignments);
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

  if (args[0] == "add") {
    if (args.size() < 3) {
      return Err(Location(), "Invalid add command.",
                 "Usage: add <attribute> <value(s)>");
    }
    ASSIGN_OR_RETURN(std::vector<Value> values,
                     ParseValues(base::make_span(args).subspan(2)));
    return AddToAttributeCommand(args[1], std::move(values));
  } else if (args[0] == "delete") {
    if (args.size() != 1) {
      return Err(Location(), "Invalid delete command.", "Usage: delete");
    }
    return DeleteCommand();
  } else if (args[0] == "move") {
    if (args.size() < 4) {
      return Err(Location(), "Invalid move command.",
                 "Usage: move <from_attribute> <to_attribute> <value(s)>");
    }
    ASSIGN_OR_RETURN(std::vector<Value> values,
                     ParseValues(base::make_span(args).subspan(3)));
    return MoveCommand(args[1], args[2], std::move(values));
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
