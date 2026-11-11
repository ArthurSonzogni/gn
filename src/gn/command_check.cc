// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "gn/commands.h"
#include "gn/deps_iterator.h"
#include "gn/header_checker.h"
#include "gn/setup.h"
#include "gn/standard_out.h"
#include "gn/switches.h"
#include "gn/target.h"
#include "gn/trace.h"

namespace commands {

const char kNoGnCheck_Help[] =
    R"(nogncheck: Skip an include line from checking.

  GN's header checker helps validate that the includes match the build
  dependency graph. Sometimes an include might be conditional or otherwise
  problematic, but you want to specifically allow it. In this case, it can be
  allowlisted.

  Include lines containing the substring "nogncheck" will be excluded from
  header checking. The most common case is a conditional include:

    #if defined(ENABLE_DOOM_MELON)
    #include "tools/doom_melon/doom_melon.h"  // nogncheck
    #endif

  If the build file has a conditional dependency on the corresponding target
  that matches the conditional include, everything will always link correctly:

    source_set("mytarget") {
      ...
      if (enable_doom_melon) {
        defines = [ "ENABLE_DOOM_MELON" ]
        deps += [ "//tools/doom_melon" ]
      }

  But GN's header checker does not understand preprocessor directives, won't
  know it matches the build dependencies, and will flag this include as
  incorrect when the condition is false.

More information

  The topic "gn help check" has general information on how checking works and
  advice on fixing problems. Targets can also opt-out of checking, see
  "gn help check_includes".
)";

const char kCheck[] = "check";
const char kCheck_HelpShort[] = "check: Check header dependencies.";
const char kCheck_Help[] =
    R"(gn check <out_dir> [<label_pattern>...] [--force] [--check-generated] [--fix]
      [--with-transitive-deps]

  GN's include header checker validates that the includes for C-like source
  files match the build dependency graph.

  "gn check" is the same thing as "gn gen" with the "--check" flag except that
  this command does not write out any build files. It's intended to be an easy
  way to manually trigger include file checking.

  Each <label_pattern> can be an exact label or a pattern that matches more
  than one (although not general regular expressions). If specified, only
  those matching targets will be checked. See "gn help label_pattern" for
  details.

Command-specific switches

  --check-generated
      Generated files are normally not checked since they do not exist
      until after a build. With this flag, those generated files that
      can be found on disk are also checked. The .gn file can exclude the
      generated files of some targets with no_check_generated_targets (see
      "gn help dotfile").

  --check-system
     Check system style includes (using <angle brackets>) in addition to
     "double quote" includes.

  --fix
      Automatically apply suggestions to resolve header dependency errors.

)" DEFAULT_TOOLCHAIN_SWITCH_HELP
    R"(
  --force
      Ignores specifications of "check_includes = false" and checks all
      target's files that match the target label.

  --with-transitive-deps
      Also checks the transitive dependencies, including data_deps, of the
      targets that match the label patterns. These are the targets that
      building the matching targets builds. The lists in the .gn file are
      applied (see "What gets checked").

What gets checked

  The .gn file may specify a list of targets to be checked in the list
  check_targets (see "gn help dotfile"). Alternatively, the .gn file may
  specify a list of targets not to be checked in no_check_targets. It may also
  list targets whose generated files are not checked with --check-generated in
  no_check_generated_targets. If a label pattern is specified on the command
  line, none of these lists is used unless --with-transitive-deps is
  given.

  Targets can opt-out from checking with "check_includes = false" (see
  "gn help check_includes").

  For targets being checked:

    - GN opens all C-like source files in the targets to be checked and scans
      the top for includes.

    - Generated files (that might not exist yet) are ignored unless
      the --check-generated flag is provided.

    - Includes with a "nogncheck" annotation are skipped (see
      "gn help nogncheck").

    - Includes using "quotes" are always checked.
        If system style checking is enabled, includes using <angle brackets>
        are also checked.

    - Include paths are assumed to be relative to any of the "include_dirs" for
      the target (including the implicit current dir).

    - GN does not run the preprocessor so will not understand conditional
      includes.

    - Only includes matching known files in the build are checked: includes
      matching unknown paths are ignored.

  For an include to be valid:

    - The included file must be in the current target, or there must be a path
      following only public dependencies to a target with the file in it
      ("gn path" is a good way to diagnose problems).

    - There can be multiple targets with an included file: only one needs to be
      valid for the include to be allowed.

    - If there are only "sources" in a target, all are considered to be public
      and can be included by other targets with a valid public dependency path.

    - If a target lists files as "public", only those files are able to be
      included by other targets. Anything in the sources will be considered
      private and will not be includable regardless of dependency paths.

    - Outputs from actions are treated like public sources on that target.

    - A target can include headers from a target that depends on it if the
      other target is annotated accordingly. See "gn help
      allow_circular_includes_from".

Advice on fixing problems

  If you have a third party project that is difficult to fix or doesn't care
  about include checks it's generally best to exclude that target from checking
  altogether via "check_includes = false".

  If you have conditional includes, make sure the build conditions and the
  preprocessor conditions match, and annotate the line with "nogncheck" (see
  "gn help nogncheck" for an example).

  If two targets are hopelessly intertwined, use the
  "allow_circular_includes_from" annotation. Ideally each should have identical
  dependencies so configs inherited from those dependencies are consistent (see
  "gn help allow_circular_includes_from").

  If you have a standalone header file or files that need to be shared between
  a few targets, you can consider making a source_set listing only those
  headers as public sources. With only header files, the source set will be a
  no-op from a build perspective, but will give a central place to refer to
  those headers. That source set's files will still need to pass "gn check" in
  isolation.

  In rare cases it makes sense to list a header in more than one target if it
  could be considered conceptually a member of both.

Examples

  gn check out/Debug
      Check everything.

  gn check out/Default //foo:bar
      Check only the files in the //foo:bar target.

  gn check out/Default "//foo/*
      Check only the files in targets in the //foo directory tree.

  gn check out/Default --with-transitive-deps //foo:bar //foo:baz
      Check the files in //foo:bar, //foo:baz and their transitive
      dependencies, except targets excluded by the lists in the .gn file.
)";

namespace {

// Returns the targets matching label_patterns, and their transitive deps if
// with_transitive_deps is set. Returns std::nullopt if the patterns are
// invalid or match no target.
std::optional<std::vector<const Target*>> ResolveCandidates(
    Setup* setup,
    const std::vector<std::string>& label_patterns,
    bool with_transitive_deps,
    bool default_toolchain_only) {
  UniqueVector<const Target*> target_matches;
  UniqueVector<const Config*> config_matches;
  UniqueVector<const Toolchain*> toolchain_matches;
  UniqueVector<SourceFile> file_matches;
  if (!ResolveFromCommandLineInput(
          setup, label_patterns, default_toolchain_only, &target_matches,
          &config_matches, &toolchain_matches, &file_matches))
    return std::nullopt;

  if (target_matches.size() == 0) {
    OutputString("No matching targets.\n");
    return std::nullopt;
  }

  if (with_transitive_deps) {
    // Walk all public, private and data deps, which are what a build of the
    // matching targets builds. target_matches grows while it is iterated.
    for (size_t i = 0; i < target_matches.size(); i++) {
      const Target* target = target_matches[i];
      for (const auto& pair : target->GetDeps(Target::DEPS_ALL))
        target_matches.push_back(pair.ptr);
    }
  }
  return target_matches.release();
}

}  // namespace

std::optional<TargetsToCheck> GetTargetsToCheck(
    Setup* setup,
    const std::vector<const Target*>& all_targets,
    const std::vector<std::string>& label_patterns,
    bool with_transitive_deps,
    bool default_toolchain_only) {
  TargetsToCheck result;
  if (label_patterns.empty()) {
    result.candidates = all_targets;
  } else if (auto resolved =
                 ResolveCandidates(setup, label_patterns, with_transitive_deps,
                                   default_toolchain_only)) {
    result.candidates = std::move(*resolved);
  } else {
    return std::nullopt;
  }

  if (!label_patterns.empty() && !with_transitive_deps) {
    // An explicit check of targets bypasses the lists in the .gn file.
    result.to_check = result.candidates;
    return result;
  }

  if (setup->check_patterns()) {
    FilterTargetsByPatterns(result.candidates, *setup->check_patterns(),
                            &result.to_check);
  } else if (setup->no_check_patterns()) {
    FilterOutTargetsByPatterns(result.candidates, *setup->no_check_patterns(),
                               &result.to_check);
  } else {
    result.to_check = result.candidates;
  }
  FilterTargetsByPatterns(result.to_check, setup->no_check_generated_patterns(),
                          &result.no_check_generated);
  return result;
}

int RunCheck(const std::vector<std::string>& args) {
  if (args.empty()) {
    Err(Location(), "Unknown command format. See \"gn help check\"",
        "Usage: \"gn check <out_dir> [<label_pattern>...]\"")
        .PrintToStdout();
    return 1;
  }

  // Deliberately leaked to avoid expensive process teardown.
  Setup* setup = new Setup();
  if (!setup->DoSetup(args[0], false))
    return 1;
  if (!setup->Run())
    return 1;

  const base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  bool default_toolchain_only = cmdline->HasSwitch(switches::kDefaultToolchain);
  // TODO(crbug.com/584730525): Consider checking transitive deps by default.
  bool with_transitive_deps = cmdline->HasSwitch("with-transitive-deps");
  bool force = cmdline->HasSwitch("force");
  bool check_generated = cmdline->HasSwitch("check-generated");
  bool check_system =
      setup->check_system_includes() || cmdline->HasSwitch("check-system");
  bool fix = cmdline->HasSwitch("fix");

  std::vector<std::string> label_patterns(args.begin() + 1, args.end());
  if (with_transitive_deps && label_patterns.empty()) {
    Err(Location(), "--with-transitive-deps requires a label pattern.",
        "Usage: \"gn check <out_dir> --with-transitive-deps "
        "<label_pattern>...\"")
        .PrintToStdout();
    return 1;
  }

  std::vector<const Target*> all_targets =
      setup->builder().GetAllResolvedTargets();
  std::optional<TargetsToCheck> targets =
      GetTargetsToCheck(setup, all_targets, label_patterns,
                        with_transitive_deps, default_toolchain_only);
  if (!targets)
    return 1;

  // no_check_generated_targets only matters when generated files are checked.
  if (!check_generated)
    targets->no_check_generated.clear();

  if (!CheckPublicHeaders(&setup->build_settings(), all_targets,
                          targets->to_check, targets->no_check_generated, force,
                          check_generated, check_system, fix, setup))
    return 1;

  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kQuiet)) {
    if (targets->to_check.size() != targets->candidates.size()) {
      // Tell the user about the implicit filtering since this is obscure.
      OutputString(base::StringPrintf(
          "%d targets out of %d checked based on the check_targets or "
          "no_check_targets defined in \".gn\".\n",
          static_cast<int>(targets->to_check.size()),
          static_cast<int>(targets->candidates.size())));
    }
    if (!targets->no_check_generated.empty()) {
      OutputString(base::StringPrintf(
          "Generated files of %d targets not checked based on the "
          "no_check_generated_targets defined in \".gn\".\n",
          static_cast<int>(targets->no_check_generated.size())));
    }
    OutputString("Header dependency check OK\n", DECORATION_GREEN);
  }
  return 0;
}

bool CheckPublicHeaders(const BuildSettings* build_settings,
                        const std::vector<const Target*>& all_targets,
                        const std::vector<const Target*>& to_check,
                        const std::vector<const Target*>& no_check_generated,
                        bool force_check,
                        bool check_generated,
                        bool check_system,
                        bool apply,
                        Setup* setup,
                        OutputStringFunc output_fn) {
  ScopedTrace trace(TraceItem::TRACE_CHECK_HEADERS, "Check headers");

  auto OutputString = [&](std::string_view str,
                          TextDecoration dec = DECORATION_NONE,
                          HtmlEscaping esc = DEFAULT_ESCAPING) {
    if (output_fn) {
      output_fn(str, dec, esc);
    } else {
      ::OutputString(str, dec, esc);
    }
  };

  scoped_refptr<HeaderChecker> header_checker(new HeaderChecker(
      build_settings, all_targets, check_generated, check_system));

  std::vector<HeaderChecker::Violation> violations;
  header_checker->Run(to_check, no_check_generated, force_check, &violations);

  bool remaining_violations = false;
  bool needs_separator = false;
  bool has_suggestions = false;
  TargetResolutionCache cache;
  for (auto& violation : violations) {
    if (needs_separator) {
      OutputString("___________________\n", DECORATION_YELLOW);
      needs_separator = false;
    }
    bool fixed = false;
    std::vector<std::tuple<std::string, TextDecoration, HtmlEscaping>> buf;
    if (!violation.source_file.is_null() &&
        !violation.included_file.is_null()) {
      SuggestResult exit_code = OutputSuggestions(
          all_targets, build_settings,
          violation.source_target->label().GetToolchainLabel(),
          violation.source_file.value(), violation.source_target,
          violation.included_file.value(),
          [&](std::string_view str, TextDecoration dec, HtmlEscaping esc) {
            buf.emplace_back(str, dec, esc);
          },
          cache, /*must_be_file=*/true, apply, setup);
      fixed = apply && (exit_code == SuggestResult::kSuccess);
      if (!buf.empty()) {
        has_suggestions = true;
      }
    }

    auto& err = violation.error;
    if (fixed) {
      // Strip the help text. We shouldn't be overly verbose if we've already
      // fixed the problem, just provide the context of the #include we were
      // trying to fix.
      err = Err(err.location(), err.message());
    } else {
      remaining_violations = true;
    }

    bool printed = true;
    if (output_fn) {
      OutputString(err.to_string());
    } else {
      printed = err.PrintToStdout();
    }

    if (printed) {
      for (const auto& [str, dec, esc] : buf) {
        OutputString(str, dec, esc);
      }
      needs_separator = true;
    } else if (apply) {
      // If we're applying suggestions and only configured to print a single
      // error, we can't yet stop because we might be able to apply more.
    } else {
      break;
    }
  }

  if (!apply && has_suggestions) {
    OutputString("\nTo automatically apply suggestions, add --fix.\n",
                 DECORATION_DIM);
  }

  return !remaining_violations;
}

}  // namespace commands
