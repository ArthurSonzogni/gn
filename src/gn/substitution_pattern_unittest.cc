// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/substitution_pattern.h"

#include "gn/err.h"
#include "gn/rust_substitution_type.h"
#include "util/test/test.h"

TEST(SubstitutionPattern, ParseLiteral) {
  SubstitutionPattern pattern;
  Err err;
  EXPECT_TRUE(pattern.Parse("This is a literal", nullptr, &err));
  EXPECT_SUCCESS(err);
  ASSERT_EQ(1u, pattern.ranges().size());
  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[0].type);
  EXPECT_EQ("This is a literal", pattern.ranges()[0].literal);
}

TEST(SubstitutionPattern, ParseComplex) {
  SubstitutionPattern pattern;
  Err err;
  EXPECT_TRUE(pattern.Parse(
      "AA{{source}}{{source_name_part}}BB{{source_file_part}}", nullptr, &err));
  EXPECT_SUCCESS(err);
  ASSERT_EQ(5u, pattern.ranges().size());

  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[0].type);
  EXPECT_EQ("AA", pattern.ranges()[0].literal);
  EXPECT_EQ(&SubstitutionSource, pattern.ranges()[1].type);
  EXPECT_EQ(&SubstitutionSourceNamePart, pattern.ranges()[2].type);
  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[3].type);
  EXPECT_EQ("BB", pattern.ranges()[3].literal);
  EXPECT_EQ(&SubstitutionSourceFilePart, pattern.ranges()[4].type);
}

TEST(SubstitutionPattern, ParseErrors) {
  SubstitutionPattern pattern;
  Err err;
  EXPECT_FALSE(pattern.Parse("AA{{source", nullptr, &err));
  EXPECT_TRUE(err.has_error());

  err = Err();
  EXPECT_FALSE(pattern.Parse("{{source_of_evil}}", nullptr, &err));
  EXPECT_TRUE(err.has_error());

  err = Err();
  EXPECT_FALSE(pattern.Parse("{{source{{source}}", nullptr, &err));
  EXPECT_TRUE(err.has_error());
}

TEST(SubstitutionPattern, ParseRust) {
  SubstitutionPattern pattern;
  Err err;
  EXPECT_TRUE(pattern.Parse(
      "AA{{rustflags}}{{rustenv}}BB{{crate_name}}{{rustdeps}}CC{{externs}}",
      nullptr, &err));
  EXPECT_SUCCESS(err);
  ASSERT_EQ(8u, pattern.ranges().size());

  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[0].type);
  EXPECT_EQ("AA", pattern.ranges()[0].literal);
  EXPECT_EQ(&kRustSubstitutionRustFlags, pattern.ranges()[1].type);
  EXPECT_EQ(&kRustSubstitutionRustEnv, pattern.ranges()[2].type);
  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[3].type);
  EXPECT_EQ("BB", pattern.ranges()[3].literal);
  EXPECT_EQ(&kRustSubstitutionCrateName, pattern.ranges()[4].type);
  EXPECT_EQ(&kRustSubstitutionRustDeps, pattern.ranges()[5].type);
  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[6].type);
  EXPECT_EQ("CC", pattern.ranges()[6].literal);
  EXPECT_EQ(&kRustSubstitutionExterns, pattern.ranges()[7].type);
}

TEST(SubstitutionPattern, ParseStarlark) {
  SubstitutionPattern pattern;
  Err err;
  EXPECT_TRUE(pattern.Parse("AA{{starlark:custom_flag}}BB{{starlark:other}}",
                            nullptr, &err));
  EXPECT_SUCCESS(err);
  ASSERT_EQ(4u, pattern.ranges().size());

  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[0].type);
  EXPECT_EQ("AA", pattern.ranges()[0].literal);

  const Substitution* custom_flag = pattern.ranges()[1].type;
  EXPECT_TRUE(IsValidToolSubstitution(custom_flag));
  EXPECT_STREQ("{{starlark:custom_flag}}", custom_flag->name);
  EXPECT_STREQ("starlark_custom_flag", custom_flag->ninja_name);

  EXPECT_EQ(&SubstitutionLiteral, pattern.ranges()[2].type);
  EXPECT_EQ("BB", pattern.ranges()[2].literal);

  const Substitution* other = pattern.ranges()[3].type;
  EXPECT_TRUE(IsValidToolSubstitution(other));
  EXPECT_STREQ("{{starlark:other}}", other->name);
  EXPECT_STREQ("starlark_other", other->ninja_name);

  // Parsing the same starlark substitution again should return the identical
  // pointer.
  SubstitutionPattern pattern_repeat;
  EXPECT_TRUE(pattern_repeat.Parse("{{starlark:custom_flag}}", nullptr, &err));
  EXPECT_SUCCESS(err);
  ASSERT_EQ(1u, pattern_repeat.ranges().size());
  EXPECT_EQ(custom_flag, pattern_repeat.ranges()[0].type);
}

TEST(SubstitutionPattern, ParseStarlarkErrors) {
  SubstitutionPattern pattern;
  Err err;

  // Empty starlark substitution name.
  EXPECT_FALSE(pattern.Parse("{{starlark:}}", nullptr, &err));
  EXPECT_TRUE(err.has_error());

  // Starlark substitution name with invalid characters (hyphen).
  err = Err();
  EXPECT_FALSE(pattern.Parse("{{starlark:bad-flag}}", nullptr, &err));
  EXPECT_TRUE(err.has_error());

  // Starlark substitution name with invalid characters (dot).
  err = Err();
  EXPECT_FALSE(pattern.Parse("{{starlark:bad.flag}}", nullptr, &err));
  EXPECT_TRUE(err.has_error());

  // Starlark substitution name with uppercase characters.
  err = Err();
  EXPECT_FALSE(pattern.Parse("{{starlark:badFlag}}", nullptr, &err));
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(
      "The starlark substitution name \"badFlag\" must contain only lowercase "
      "letters and _.",
      err.help_text());

  // Unclosed starlark substitution.
  err = Err();
  EXPECT_FALSE(pattern.Parse("{{starlark:unclosed", nullptr, &err));
  EXPECT_TRUE(err.has_error());
}