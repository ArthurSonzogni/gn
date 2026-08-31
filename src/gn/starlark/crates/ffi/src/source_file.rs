// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::SourceFile;

impl SourceFile {
    /// Converts a GN SourceFile to a starlark File relative to the build
    /// directory.
    pub fn to_rust(&self, settings: &crate::Settings) -> types::File {
        // Safety: source_file_to_output_path returns a string backed by an interned
        // StringAtom and is static.
        types::File::new(unsafe {
            types::util::extend_lifetime(crate::bridge::source_file_to_output_path(settings, self))
        })
    }
}
