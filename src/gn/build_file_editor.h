// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_BUILD_FILE_EDITOR_H_
#define TOOLS_GN_BUILD_FILE_EDITOR_H_

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "gn/err.h"
#include "gn/input_file.h"
#include "gn/label.h"
#include "gn/label_pattern.h"
#include "gn/parse_tree.h"
#include "gn/source_file.h"

class BuildSettings;
class Loader;

struct EditState;
struct EditTarget;

// A TreeNode represents a node in a tree.
// It fundamentally represents a ParseNode, but differs from one as it
// understands where it exists in the tree.
class TreeNode {
 public:
  explicit TreeNode(std::vector<ParseNode*> stack) : stack_(std::move(stack)) {
    DCHECK(!stack_.empty());
  }

  ParseNode* node() const { return stack_.back(); }
  ParseNode* parent() const {
    return stack_.size() > 1 ? stack_[stack_.size() - 2] : nullptr;
  }

  // Returns the BinaryOpNode if the node is an assignment ("=" or "+=").
  BinaryOpNode* AsAssignment() const;

  // Returns whether the node is conditional in a target.
  // Note that if the target itself is conditional, this will return false.
  bool is_conditional() const;

  // Returns whether the node is a "+=" or "-=" operation.
  bool is_modification() const;

  // Adds a todo comment to the build file to show the user where manual
  // intervention is required.
  void add_todo(EditState& state, const EditTarget& target) const;

  // Removes self from the tree, or adds a TODO suggesting that it should
  // probably be removed.
  void RemoveSelf(EditState& state, const EditTarget& target) const;

  // Removes self from the tree unconditionally without adding TODO comments.
  void RemoveSelfUnconditionally() const;

  ParseNode* operator->() const { return stack_.back(); }

  const std::vector<ParseNode*>& stack() const { return stack_; }

  TreeNode Descend(ParseNode* child) const;

 private:
  std::vector<ParseNode*> stack_;
};

template <typename T>
void FindStatementRecursive(
    ParseNode* node,
    std::vector<ParseNode*>& stack,
    const std::function<std::optional<T>(TreeNode&)>& transform,
    std::vector<T>* results) {
  if (!node)
    return;

  stack.push_back(node);

  TreeNode node_ref(stack);
  if (auto mapped = transform(node_ref)) {
    results->push_back(std::move(*mapped));
  }

  if (auto* block = node->AsBlockMut()) {
    for (const auto& stmt : block->statements()) {
      FindStatementRecursive(stmt.get(), stack, transform, results);
    }
  } else if (auto* condition = node->AsConditionMut()) {
    FindStatementRecursive(condition->if_true(), stack, transform, results);
    if (condition->if_false()) {
      FindStatementRecursive(condition->if_false(), stack, transform, results);
    }
  } else if (auto* func = node->AsFunctionCallMut(); func && func->block()) {
    FindStatementRecursive(func->block(), stack, transform, results);
  }

  stack.pop_back();
}

// Returns a vector of nodes matching a condition.
// May also apply a transformation to add useful metadata to them.
template <typename T>
std::vector<T> FindStatement(
    ParseNode* root,
    const std::function<std::optional<T>(TreeNode&)>& transform) {
  std::vector<T> results;
  std::vector<ParseNode*> stack;
  FindStatementRecursive<T>(root, stack, transform, &results);
  return results;
}

// Finds an element in an assignment expression ("=" or "+=") whose right-hand
// side likely evaluates to a list.
std::vector<TreeNode> FindListElementInAssignment(const EditTarget& target,
                                                  const TreeNode& root,
                                                  const Value& value);

// Finds the first list node within an assignment expression.
std::optional<ListNode*> FindListInAssignment(const TreeNode& assignment);

// Represents a set of patterns within a build file.
class LabelMatcher {
 public:
  LabelMatcher(SourceDir source_dir, const std::vector<LabelPattern>& patterns);

  enum MatchType {
    // Target does not match any pattern in this build file.
    NONE,
    // Target was explicitly named (e.g. "//foo:bar"). Unmatched explicit
    // targets will trigger an error when done() is called.
    EXACT,
    // Target matched a wildcard pattern (e.g. "//foo:*" or "//foo/*").
    GLOB,
  };

  // Checks whether a label was a match for a given pattern.
  MatchType matches(const std::string& name);

  // Call this when done editing a build file.
  // Any explicitly requested targets that were unused will trigger an error.
  Err done() const;

 private:
  SourceDir source_dir_;
  bool globbed_ = false;
  std::unordered_map<std::string, bool> used_;
};

// Represents a build target to be edited.
struct EditTarget {
  // Calculates all =, +=, and -= of a given attribute.
  std::vector<TreeNode> assignments(std::string_view attr) const;

  // Emits a warning to the user.
  void add_warning(EditState& state, std::string_view message) const;

  // True if the target was explicitly requested to be edited.
  // This is relevant, because if the user requests something like
  // "remove deps //dep" //:*, then we should not print warnings
  // if not all targets depend on //dep.
  // On the other hand, if the user says "remove deps //dep" //:foo,
  // and we can't find a dep on //dep, we should warn them about it.
  bool is_explicit;
  Label label;
  TreeNode node;
  BlockNode* block;
};

// Represents a build file to be edited.
class BuildFile {
 public:
  static Result<BuildFile> Create(const BuildSettings* build_settings,
                                  const SourceFile& source_file,
                                  const std::vector<LabelPattern>& patterns);

  const SourceFile& source_file() const { return source_file_; }
  ParseNode* root() const { return tree_root_.get(); }
  LabelMatcher& label_matcher() { return label_matcher_; }

  // Returns a generic location at the start of the file.
  // This is relevant because generated nodes won't have location information.
  Location location() const;

  // Returns all targets matching the patterns.
  std::vector<EditTarget> targets();

  // Creates a node to insert into the graph.
  std::unique_ptr<ParseNode> to_node(const Value& value);

  // Creates a node for an identifier.
  std::unique_ptr<IdentifierNode> create_identifier(std::string_view value);
  // Creates a node for `a = b`
  std::unique_ptr<BinaryOpNode> create_assignment(
      std::string_view name,
      std::unique_ptr<ParseNode> value);

  // Serializes the AST to the build file if it has changed.
  // Returns Ok(true) if the file was written, Ok(false) if it was unchanged.
  Result<bool> Write();

 private:
  BuildFile(const BuildSettings* build_settings,
            SourceFile source_file,
            std::unique_ptr<InputFile> input_file,
            std::unique_ptr<ParseNode> tree_root,
            LabelMatcher label_matcher);

  const BuildSettings* build_settings_;
  SourceFile source_file_;
  std::unique_ptr<InputFile> input_file_;
  std::unique_ptr<ParseNode> tree_root_;
  // Our custom ParseNodes generated by to_node contain string views.
  // We store objects containing the underlying strings here to ensure they
  // live long enough.
  std::vector<std::unique_ptr<InputFile>> extra_files_;
  LabelMatcher label_matcher_;
};

// Resolves a list of LabelPatterns into the union of the build files they
// cover.
Result<std::vector<BuildFile>> ResolvePatternsToBuildFiles(
    const BuildSettings* build_settings,
    const Loader* loader,
    const std::vector<LabelPattern>& patterns);

#endif  // TOOLS_GN_BUILD_FILE_EDITOR_H_
