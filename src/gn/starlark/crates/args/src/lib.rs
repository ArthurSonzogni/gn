// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod arg_sharing;
pub mod args;
pub mod args_list;
pub mod errors;
pub mod expand;
pub mod formatter;
#[cfg(test)]
mod tests;

pub use arg_sharing::ArgSharingPlans;
pub use args::{ArgValue, Args, FrozenArgs, args_methods};
pub use args_list::{ArgsLike, ArgsList};
pub(crate) use errors::Error;
pub use expand::ArgVisitor;
pub use formatter::Formatter;
