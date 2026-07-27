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

import com.google.common.collect.ImmutableSet;
import com.starrocks.common.Config;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Resolves the effective set of function names allowed by VALIDATE.
 *
 * <p>The effective set is {@link #DEFAULT_ALLOWED_FUNCTIONS} plus any names configured in
 * {@code validate_allowed_functions} ({@code fe.conf} / ConfigMap / {@code ADMIN SET}): config is <em>additive</em>,
 * so the safe default baseline is always allowed and an operator can only widen it. Names are compared
 * case-insensitively (lowercased).
 *
 * <p>{@code validate_allowed_functions} is a mutable, per-FE config: {@code ADMIN SET FRONTEND CONFIG} without
 * {@code ALL} only changes the FE handling the statement, and VALIDATE is {@code NO_FORWARD} (see
 * {@link com.starrocks.sql.ast.celonis.validate.ValidateStmt}), so the same VALIDATE query can report different
 * results on different FEs if the config drifts between them.
 */
public final class ValidateFunctionWhitelist {

    /**
     * Default whitelist of read-only, access-safe scalar/aggregate functions.
     *
     * <p>The geospatial and a handful of the numeric/string entries are kept in sync with Celonis PQL's
     * {@code create_sql_table} allow-list — see {@code FUNCTION_ALLOW_LIST} in the celostar repo at
     * {@code pql2sql/src/main/java/com/celonis/celostar/pql2sql/sqlparsing/jsql/Validation.java}. That list
     * deliberately excludes non-deterministic functions (rand/uuid/now/sysdate); this default is a superset that
     * still includes {@code now} and other date/time helpers, since VALIDATE only gates access, not determinism.
     */
    static final Set<String> DEFAULT_ALLOWED_FUNCTIONS = ImmutableSet.of(
            // string
            "concat", "concat_ws", "substr", "substring", "lower", "upper", "trim", "ltrim", "rtrim",
            "length", "char_length", "character_length", "lpad", "rpad", "replace", "reverse", "left", "right",
            "locate", "instr", "split_part", "repeat", "ascii", "space", "starts_with", "ends_with",
            "regexp_extract", "regexp_replace", "regexp_count", "strleft", "strright", "null_or_empty",
            // date / time
            "now", "current_timestamp", "current_date", "current_time", "curdate", "curtime", "date",
            "date_add", "date_sub", "adddate", "subdate", "date_trunc", "date_format", "datediff", "timediff",
            "timestampdiff", "timestampadd", "year", "quarter", "month", "week", "day", "dayofmonth", "dayofweek",
            "dayofyear", "weekofyear", "hour", "minute", "second", "to_date", "str_to_date", "unix_timestamp",
            "from_unixtime", "to_days", "last_day", "months_add", "years_add", "days_add", "hours_add",
            // math
            "abs", "ceil", "ceiling", "floor", "round", "truncate", "sqrt", "cbrt", "pow", "power", "exp",
            "ln", "log", "log2", "log10", "sign", "mod", "pmod", "pi", "greatest", "least", "positive", "negative",
            "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "degrees", "radians",
            // bitwise (from celostar FUNCTION_ALLOW_LIST)
            "bit_and", "bit_or", "bit_xor",
            // conditional
            "if", "ifnull", "nullif", "coalesce", "nvl", "nvl2", "case",
            // cast / conversion
            "cast", "convert",
            // aggregates
            "count", "sum", "min", "max", "avg", "group_concat", "std", "stddev", "stddev_samp", "stddev_pop",
            "variance", "var_samp", "var_pop", "approx_count_distinct", "any_value",
            // grouping (report which GROUPING SETS/CUBE/ROLLUP columns produced a row; no data access)
            "grouping", "grouping_id",
            // geospatial (kept in sync with celostar FUNCTION_ALLOW_LIST)
            "st_contains", "st_distance_sphere", "st_geometryfromtext", "st_linefromtext", "st_point",
            "st_polyfromtext", "st_polygon",
            // array
            "array_length", "cardinality", "array_contains", "array_min", "array_max", "array_sum", "array_avg",
            "array_distinct", "array_sort", "array_position", "array_slice", "array_agg",
            // json (read-only accessors)
            "get_json_string", "get_json_int", "get_json_double", "get_json_bool", "json_query", "json_exists",
            "json_length");

    private ValidateFunctionWhitelist() {
    }

    public static Set<String> effectiveAllowedFunctions() {
        Set<String> allowed = new HashSet<>(DEFAULT_ALLOWED_FUNCTIONS);
        String[] configured = Config.validate_allowed_functions;
        if (configured != null) {
            for (String name : configured) {
                if (name != null && !name.isBlank()) {
                    allowed.add(name.trim().toLowerCase(Locale.ROOT));
                }
            }
        }
        return allowed;
    }
}
