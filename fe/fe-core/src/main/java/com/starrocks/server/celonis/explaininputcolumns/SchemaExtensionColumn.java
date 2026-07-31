// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.starrocks.server.celonis.explaininputcolumns;

import com.starrocks.catalog.Type;

/** A virtual schema column injected for statement-scoped analysis. Omitted types are normalized to {@link Type#NULL}. */
public record SchemaExtensionColumn(String name, Type type) {
    public SchemaExtensionColumn {
        // Type.NULL is only a best-effort placeholder when the caller does not know the extension type. It keeps
        // basic references analyzable, but type-sensitive expressions may resolve differently or fail; callers that
        // need accurate validation for those expressions should provide the actual type.
        type = type == null ? Type.NULL : type;
    }
}
