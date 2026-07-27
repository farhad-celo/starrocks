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

package com.starrocks.sql.analyzer.celonis.validate;

/**
 * A single whitelist violation reported by VALIDATE. {@code kind} is a stable machine-readable category
 * ({@code "ast_node"} or {@code "function"}); {@code detail} names the offending node type or function.
 */
public record ValidateViolation(String kind, String detail) {
    public static final String KIND_AST_NODE = "ast_node";
    public static final String KIND_FUNCTION = "function";
    public static final String KIND_STATEMENT = "statement";
}
