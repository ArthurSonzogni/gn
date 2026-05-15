// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "gn/commands.h"
#include "gn/filesystem_utils.h"
#include "gn/item.h"
#include "gn/setup.h"
#include "gn/standard_out.h"
#include "gn/target.h"

namespace commands {

const char kSuggest[] = "suggest";
const char kSuggest_HelpShort[] =
    "suggest: Suggest fixes to build graph based on includes.";
const char kSuggest_Help[] =
    R"(suggest: Suggest fixes to build graph based on includes.

  gn suggest <out_dir> includer1=included1 includer2=included2...

  Where each includer or included is either:
  * A label
  * A module name (usually the same as the label)
  * A file path relative to the build directory
  * An absolute file path (eg. "//foo/bar.txt")

  Eg. gn suggest out_dir path/to/target.cc=foo/bar.h

  Will print a suggestion like:
  Request: path/to/target.cc wants to depend on foo/bar.h
  Suggestion: add deps = [ "//foo:bar" ] to "//path/to:target" (defined in //path/to/BUILD.gn:1234)
)";

constexpr std::string_view kPrivateSuffix = "_Private";

namespace {
// Determines whether a source file is in either the public or private API of a
// target.
std::optional<commands::ApiScope> DepKind(const Target* target,
                                          const SourceFile& file) {
  for (const auto& source : target->sources()) {
    if (source == file) {
      return target->all_headers_public() &&
                     file.GetType() == SourceFile::SOURCE_H
                 ? commands::ApiScope::kPublic
                 : commands::ApiScope::kPrivate;
    }
  }
  for (const auto& header : target->public_headers()) {
    if (header == file) {
      return commands::ApiScope::kPublic;
    }
  }
  return std::nullopt;
}

// Finds all targets that use a file as a source from a specific toolchain and
// adds them to results. Checks every toolchain if current_toolchain is null.
bool AddToolchainSources(
    const std::vector<const Target*>& all_targets,
    const Label* current_toolchain,
    const SourceFile& file,
    std::vector<std::pair<const Target*, commands::ApiScope>>& results) {
  for (const Target* target : all_targets) {
    if (!current_toolchain ||
        target->label().GetToolchainLabel() == *current_toolchain) {
      if (auto dep_kind = DepKind(target, file); dep_kind.has_value()) {
        results.emplace_back(target, *dep_kind);
      }
    }
  }
  return !results.empty();
}

SourceFile ResolveFilePath(const BuildSettings* build_settings,
                           std::string_view input) {
  if (input.starts_with("//")) {
    SourceFile file = SourceFile(input);
    if (base::PathExists(build_settings->GetFullPath(file))) {
      return file;
    }
    return SourceFile();
  }
  // Resolve relative to the output directory.
  // This is because the user is most likely running this based on an error
  // message from clang, which gives paths relative to the output directory to
  // be unambiguous.
  Err err;
  SourceFile file = build_settings->build_dir().ResolveRelativeFile(
      Value(nullptr, std::string(input)), &err);
  if (!err.has_error() && base::PathExists(build_settings->GetFullPath(file))) {
    return file;
  }
  return SourceFile();
}

// Returns true if depending on target is supposed to give you access to
// everything in the underlying target.
bool Exposes(const Target& target, const Target& underlying) {
  std::vector<const Target*> stack = {&target};
  std::unordered_set<const Target*> visited;
  while (!stack.empty()) {
    const Target* current = stack.back();
    stack.pop_back();
    if (visited.insert(current).second) {
      if (current == &underlying) {
        return true;
      }
      // If we have no headers and no sources, then the only use of depending
      // on this target is to gain access to its dependencies.
      if (current->sources().empty() && current->public_headers().empty()) {
        for (const auto& dep : current->public_deps()) {
          stack.push_back(dep.ptr);
        }
        // If you declare `public_deps = ...` on a group, it shows up as a
        // private dep. Probably because groups don't distinguish between
        // public and private deps.
        if (current->output_type() == Target::GROUP) {
          for (const auto& dep : current->private_deps()) {
            stack.push_back(dep.ptr);
          }
        }
      }
    }
  }
  return false;
}

// Finds the shortest dependency path from `from` to `to`.
// Returns a vector where the first element is `from` and the last is `to`.
// Returns the empty vector if no path was found.
std::vector<const Target*> FindDependencyPath(const Target* from,
                                              const Target* to) {
  std::deque<const Target*> queue;
  std::unordered_map<const Target*, const Target*> parents;
  parents[from] = nullptr;
  queue.push_back(from);

  const Target* cur = nullptr;
  while (!queue.empty()) {
    cur = queue.front();
    queue.pop_front();
    if (cur == to) {
      break;
    }

    auto add_deps = [&](const LabelTargetVector& deps) {
      for (const auto& dep : deps) {
        if (dep.ptr) {
          if (parents.emplace(dep.ptr, cur).second) {
            queue.push_back(dep.ptr);
          }
        }
      }
    };

    add_deps(cur->public_deps());
    add_deps(cur->private_deps());
  }

  if (cur != to) {
    return {};
  }

  std::vector<const Target*> path;
  while (cur != nullptr) {
    path.push_back(cur);
    cur = parents[cur];
  }
  std::reverse(path.begin(), path.end());
  return path;
}

}  // namespace

// Resolves an input to a list of targets, and whether each are private.
// The input can be:
// * A module name for a target
// * A target label
// * A file path, which attempts to resolve to:
//   * Targets defined in the current toolchain that contain the file
//   * Targets defined in the default toolchain that contain the file
//   * Targets defined in any toolchain that contain the file
std::pair<std::vector<std::pair<const Target*, commands::ApiScope>>, bool>
ResolveSuggestionToTarget(const BuildSettings* build_settings,
                          const std::vector<const Target*>& all_targets,
                          const Label& current_toolchain,
                          std::string_view input) {
  auto sort_results = [](auto& vec) {
    std::sort(vec.begin(), vec.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.first->label() < rhs.first->label();
    });
  };
  std::vector<std::pair<const Target*, commands::ApiScope>> results;
  std::string_view module_name = input;
  commands::ApiScope is_private = commands::ApiScope::kPublic;
  if (module_name.ends_with(kPrivateSuffix)) {
    is_private = commands::ApiScope::kPrivate;
    module_name.remove_suffix(kPrivateSuffix.size());
  }

  // Try to resolve as a module name.
  for (const Target* target : all_targets) {
    if (target->module_name() == module_name) {
      results.emplace_back(target, is_private);
    }
  }
  if (!results.empty()) {
    sort_results(results);
    return {results, true};
  }

  // If that doesn't work, try to resolve as an absolute target label.
  if (input.starts_with("//") && input.find(':') != std::string_view::npos) {
    Err err;
    Label want;
    Value input_value(nullptr, std::string(input));
    want = Label::Resolve(SourceDir("//"), build_settings->root_path_utf8(),
                          current_toolchain, input_value, &err);
    if (!err.has_error()) {
      for (const Target* target : all_targets) {
        if (target->label() == want) {
          results.emplace_back(target, is_private);
          // We know each label corresponds to exactly one target, so we don't
          // need to keep going.
          return {results, true};
        }
      }
    }
  }

  // If that doesn't work, try to resolve as a file path.
  SourceFile file = ResolveFilePath(build_settings, input);
  if (file.is_null()) {
    return {results, false};
  }

  // If we see //foo(:toolchain) request bar.h, prefer //:bar(:toolchain)
  // over other toolchains.
  if (!AddToolchainSources(all_targets, &current_toolchain, file, results)) {
    AddToolchainSources(all_targets, nullptr, file, results);
  }
  sort_results(results);
  return {results, true};
}

bool OutputSuggestions(const std::vector<const Target*>& all_targets,
                       const BuildSettings* build_settings,
                       const Label& default_toolchain,
                       std::string_view includer_name,
                       std::string_view included_name,
                       OutputStringFunc output_fn) {
  auto OutputString =
      [&](std::string_view str, TextDecoration dec = DECORATION_NONE,
          HtmlEscaping esc = DEFAULT_ESCAPING) { output_fn(str, dec, esc); };

  constexpr auto kLabelLike = TextDecoration::DECORATION_GREEN;

  auto StartSuggestion = [&]() {
    OutputString("Suggestion: ", TextDecoration::DECORATION_BLUE);
  };
  auto StartWarning = [&]() {
    OutputString("Warning: ", TextDecoration::DECORATION_YELLOW);
  };
  auto StartError = [&]() {
    OutputString("Error: ", TextDecoration::DECORATION_RED);
  };

  auto OutputQuoted = [&](std::string_view message) {
    OutputString("\"", kLabelLike);
    OutputString(message, kLabelLike);
    OutputString("\"", kLabelLike);
  };

  auto OutputDefinition = [&](const Target* target) {
    OutputString(":", kLabelLike);
    OutputString(target->label().name(), kLabelLike);
    OutputString(" (defined at ");
    OutputString(target->user_friendly_location().Describe(false), kLabelLike);
    OutputString(")");
  };

  Label current_toolchain = default_toolchain;
  auto OutputTarget = [&current_toolchain,
                       &OutputString](const Target* target) {
    OutputString(target->label().GetUserVisibleName(current_toolchain),
                 kLabelLike);
  };

  auto OutputInsertionHint = [&](std::string_view key,
                                 const std::vector<std::string>& candidates,
                                 const Target* target) {
    bool plural = candidates.size() != 1;
    StartSuggestion();
    if (plural) {
      OutputString("Add one of the following to ");
      OutputString(key);
      OutputString(" in ");
    } else {
      OutputString("Add ");
      OutputString(key);
      OutputString(" = [ ");
      OutputQuoted(candidates.front());
      OutputString(" ] to ");
    }
    OutputDefinition(target);
    if (current_toolchain != default_toolchain) {
      OutputString(" for toolchain ");
      OutputString(
          target->label().GetToolchainLabel().GetUserVisibleName(false),
          kLabelLike);
    }
    if (plural) {
      OutputString(":\n");
      for (const auto& candidate : candidates) {
        OutputString("* ");
        OutputString(candidate);
        OutputString("\n");
      }
    } else {
      OutputString("\n");
    }
  };

  auto ResolveSuggestion = [&](std::string_view value) {
    const auto& [targets, ok] = ResolveSuggestionToTarget(
        build_settings, all_targets, current_toolchain, value);
    if (!ok) {
      StartError();
      if (value.starts_with("//")) {
        OutputString("Could not find target or file ");
        OutputQuoted(value);
      } else {
        OutputString("Unable to find ");
        OutputQuoted(value);
        OutputString(" in either the output or source root directories\n");
      }
    }
    return std::make_pair(targets, ok);
  };

  const auto& [includer_targets, includer_ok] =
      ResolveSuggestion(includer_name);
  if (!includer_ok)
    return false;

  if (includer_targets.empty()) {
    StartError();
    OutputQuoted(includer_name);
    OutputString(" did not resolve to any targets\n");
    return false;
  } else if (includer_targets.size() > 1) {
    StartError();
    OutputQuoted(includer_name);
    OutputString(" resolved to multiple targets\n");
    for (const auto& [target, is_private] : includer_targets) {
      OutputString("* ");
      OutputTarget(target);
      OutputString("\n");
    }
    return false;
  }
  const auto& [includer, dep_kind] = includer_targets.front();
  current_toolchain = includer->label().GetToolchainLabel();

  const char* dep_field =
      (dep_kind == commands::ApiScope::kPrivate) ? "deps" : "public_deps";

  const auto& [targets, ok] = ResolveSuggestion(included_name);
  if (!ok)
    return false;

  // We've passed the errors phase. At this point, everything is valid input.
  // Includer is a single target, and included is a valid target, or a file
  // that exists on disk.

  if (targets.empty()) {
    OutputQuoted(included_name);
    OutputString(" is not in the headers of any targets.\n");
    StartSuggestion();
    OutputString("Add ");
    OutputQuoted(included_name);
    OutputString(" to a target's public headers");
    return true;
  }

  std::set<Label> labels_without_toolchain;
  for (const auto& [target, _] : targets) {
    labels_without_toolchain.insert(target->label().GetWithNoToolchain());
  }
  if (labels_without_toolchain.size() == 1 &&
      targets.front().first->label().GetToolchainLabel() != current_toolchain) {
    // The resolution requires that if //:bar(:toolchain1) contained bar.h, we
    // would have returned no targets from any other toolchain. Thus, we now
    // have:
    // //:foo(:toolchain1) including bar.h -> //:bar(:toolchain2),
    // //:bar(:toolchain3)
    OutputQuoted(included_name);
    OutputString(" is defined in ");
    OutputString(labels_without_toolchain.begin()->GetUserVisibleName(false),
                 kLabelLike);
    OutputString(", but not in the toolchain ");
    OutputString(current_toolchain.GetUserVisibleName(false), kLabelLike);
    OutputString("\n");
    OutputInsertionHint("public", {std::string(included_name)},
                        targets.front().first);
    return true;
  }

  if (targets.size() > 1) {
    StartWarning();
    OutputQuoted(included_name);
    OutputString(" is ambiguous because it belongs to multiple targets:\n");
    for (const auto& [target, _] : targets) {
      OutputString("* ");
      OutputTarget(target);
      OutputString("\n");
    }
    StartSuggestion();
    OutputString(
        "Create a source_set target for the common headers and sources and "
        "have all of the above targets depend on that.");
    OutputInsertionHint(dep_field, {"$NEW_SOURCE_SET"}, includer);
    return true;
  }

  const auto& [included, included_dep_kind] = targets.front();
  if (included_dep_kind == commands::ApiScope::kPrivate) {
    StartWarning();
    OutputQuoted(included_name);
    OutputString(" is in the private API of ");
    OutputTarget(included);
    StartSuggestion();
    OutputString("Move ");
    OutputQuoted(included_name);
    OutputString(" from `sources` to `public` in ");
    OutputDefinition(included);
  }

  // TODO: There are a bunch of optimizations we can perform here to make better
  // suggestions. They may be considered in the future. Some initial thoughts
  // include:
  // * Check if included transitively depends on includer. Suggest ways to break
  // the loop.

  auto OutputDepSuggestion = [&](const std::vector<const Target*>& candidates) {
    std::vector<std::string> labels;
    for (const auto& target : candidates) {
      Label label = target->label();
      std::vector<const Target*> cycle = FindDependencyPath(target, includer);
      if (!cycle.empty()) {
        StartWarning();
        OutputTarget(target);
        OutputString(" depends on ");
        OutputTarget(includer);
        OutputString(
            ", so adding this dependency will create a dependency loop:\n");

        OutputString("  ");
        OutputTarget(includer);
        OutputString(" ->\n");

        for (size_t i = 0; i < cycle.size(); i++) {
          OutputString("  ");
          OutputTarget(cycle[i]);
          if (i + 1 < cycle.size()) {
            OutputString(" ->");
          }
          OutputString("\n");
        }

        bool has_allow_circular_includes_from = false;
        for (const Target* t : cycle) {
          if (!t->allow_circular_includes_from().empty()) {
            has_allow_circular_includes_from = true;
            StartSuggestion();
            OutputString(":", kLabelLike);
            OutputString(t->label().name(), kLabelLike);
            OutputString(" (defined at ");
            OutputString(t->user_friendly_location().Describe(false),
                         kLabelLike);
            OutputString(
                ") declares allow_circular_includes_from, which is bad style. "
                "Instead, you should remove allow_circular_includes_from by "
                "doing the following:\n");

            OutputString("source_set(\"");
            OutputString(t->label().name());
            OutputString("_sources\") {\n");
            OutputString("  # All attributes from :");
            OutputString(t->label().name());
            OutputString(" except public_deps, and any link options\n");
            OutputString(
                "  # Note that some public_deps may need to be added back "
                "based on #includes of headers.\n");
            OutputString("}\n\n");

            OutputString(Target::GetStringForOutputType(t->output_type()));
            OutputString("(\"");
            OutputString(t->label().name());
            OutputString("\") {\n");
            OutputString("  public_deps = [ \":");
            OutputString(t->label().name());
            OutputString("_sources\" ]\n");
            OutputString("  # public_deps, and any link variables from :");
            OutputString(t->label().name());
            OutputString("\n");
            OutputString("}\n");

            if (t == target) {
              label = Label(label.dir(), label.name() + "_sources",
                            label.toolchain_dir(), label.toolchain_name());
            }
            break;
          }
        }
        if (!has_allow_circular_includes_from) {
          StartSuggestion();
          OutputString(
              "Find the part of the dependency chain where there is no "
              "#include "
              "and remove that dependency.\n");
        }
      }
      labels.push_back(label.dir() == includer->label().dir()
                           ? ":" + label.name()
                           : label.GetUserVisibleName(current_toolchain));
    }

    std::sort(labels.begin(), labels.end(),
              [](std::string_view lhs, std::string_view rhs) {
                // Ensure relative labels come before absolute labels.
                bool lhs_abs = !lhs.starts_with(':');
                bool rhs_abs = !rhs.starts_with(':');
                return std::tie(lhs_abs, lhs) < std::tie(rhs_abs, rhs);
              });
    OutputInsertionHint(dep_field, labels, includer);
  };

  if (included->visibility().CanSeeMe(includer->label())) {
    OutputDepSuggestion({included});
    return true;
  }

  // Now we need to look for things that expose it.
  std::vector<const Target*> visible_candidates;
  std::vector<const Target*> nonpublic_candidates;
  std::vector<const Target*> all_candidates;
  for (const Target* candidate : all_targets) {
    if (candidate == included)
      continue;
    // Check that the toolchains are the same to avoid picking up both //:foo
    // and //:foo(other_toolchain).
    if (candidate->label().ToolchainsEqual(includer->label()) &&
        Exposes(*candidate, *included)) {
      all_candidates.push_back(candidate);
      if (candidate->visibility().CanSeeMe(includer->label())) {
        visible_candidates.push_back(candidate);
        // Check if candidate is public by checking if the empty label can see
        // it.
        if (!candidate->visibility().CanSeeMe(Label())) {
          nonpublic_candidates.push_back(candidate);
        }
      }
    }
  }

  // If, for example, we have //third_party/abseil-cpp:absl and
  // //v8:v8_abseil, and we learn that v8_abseil is not public, but we can
  // depend on it, then we are probably in the //v8 directory, and thus
  // should prefer v8_abseil.
  if (!nonpublic_candidates.empty()) {
    visible_candidates = nonpublic_candidates;
  }

  if (visible_candidates.size() == 1) {
    OutputDepSuggestion(visible_candidates);
  } else if (visible_candidates.size() > 1) {
    StartWarning();
    OutputTarget(included);
    OutputString(" is exposed via multiple targets\n");
    StartSuggestion();
    OutputString(
        "Clean up the visibility so that only one of the below targets is "
        "visible to ");
    OutputTarget(includer);
    OutputString("\n");
    OutputDepSuggestion(visible_candidates);
  } else if (all_candidates.empty()) {
    StartWarning();
    OutputTarget(included);
    OutputString(" is not visible to ");
    OutputTarget(includer);
    OutputString("\n");
    StartSuggestion();
    OutputString(
        "Carefully consider whether you want to change the visibility so that "
        "you can depend on it\n");
    OutputDepSuggestion({included});
  } else {
    StartWarning();
    OutputTarget(included);
    OutputString(
        " is exposed via the following targets, but none are visible to ");
    OutputTarget(includer);
    OutputString("\n");
    StartSuggestion();
    OutputString(
        "Carefully consider whether you want to change the visibility so that "
        "you can depend on one of them\n");
    all_candidates.push_back(included);
    OutputDepSuggestion(all_candidates);
  }

  return true;
}

int RunSuggest(const std::vector<std::string>& args) {
  constexpr auto kLabelLike = TextDecoration::DECORATION_GREEN;

  auto OutputError = [](std::string_view message) {
    OutputString("Error: ", TextDecoration::DECORATION_RED);
    OutputString(message);
  };

  auto OutputQuoted = [](std::string_view message) {
    OutputString("\"", kLabelLike);
    OutputString(message, kLabelLike);
    OutputString("\"", kLabelLike);
  };

  if (args.size() <= 1) {
    OutputError("gn suggest requires arguments. See \"gn help suggest\"\n");
    return 1;
  }

  // Deliberately leaked to avoid expensive process teardown.
  Setup* setup = new Setup;
  if (!setup->DoSetup(args[0], false) || !setup->Run())
    return 1;

  std::vector<const Target*> all_targets =
      setup->builder().GetAllResolvedTargets();

  bool success = true;
  for (size_t i = 1; i < args.size(); i++) {
    if (i != 1) {
      OutputString("\n");
    }
    std::vector<std::string_view> pair = base::SplitStringPiece(
        args[i], "=", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (pair.size() != 2) {
      OutputError("Invalid pair: " + args[i] + "\n");
      return 1;
    }
    const auto& includer = pair[0];
    const auto& included = pair[1];

    // args[0] = output directory.
    // If there's only one request, don't print which request this corresponds
    // to.
    if (args.size() > 2) {
      OutputString("Request: ", TextDecoration::DECORATION_MAGENTA);
      OutputQuoted(includer);
      OutputString(" wants to depend on ");
      OutputQuoted(included);
      OutputString(":\n");
    }

    success &= OutputSuggestions(
        all_targets, &setup->build_settings(),
        setup->loader()->default_toolchain_label(), includer, included,
        [](std::string_view str, TextDecoration dec, HtmlEscaping esc) {
          ::OutputString(str, dec, esc);
        });
  }

  return success ? 0 : 1;
}

}  // namespace commands
