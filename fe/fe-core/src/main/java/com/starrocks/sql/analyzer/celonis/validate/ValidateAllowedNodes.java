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
import com.starrocks.analysis.AnalyticExpr;
import com.starrocks.analysis.ArithmeticExpr;
import com.starrocks.analysis.ArraySliceExpr;
import com.starrocks.analysis.ArrowExpr;
import com.starrocks.analysis.BetweenPredicate;
import com.starrocks.analysis.BinaryPredicate;
import com.starrocks.analysis.BoolLiteral;
import com.starrocks.analysis.CaseExpr;
import com.starrocks.analysis.CastExpr;
import com.starrocks.analysis.CloneExpr;
import com.starrocks.analysis.CollectionElementExpr;
import com.starrocks.analysis.CompoundPredicate;
import com.starrocks.analysis.DateLiteral;
import com.starrocks.analysis.DecimalLiteral;
import com.starrocks.analysis.ExistsPredicate;
import com.starrocks.analysis.FloatLiteral;
import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.analysis.GroupingFunctionCallExpr;
import com.starrocks.analysis.InPredicate;
import com.starrocks.analysis.IntLiteral;
import com.starrocks.analysis.IsNullPredicate;
import com.starrocks.analysis.LargeInPredicate;
import com.starrocks.analysis.LargeIntLiteral;
import com.starrocks.analysis.LargeStringLiteral;
import com.starrocks.analysis.LikePredicate;
import com.starrocks.analysis.MaxLiteral;
import com.starrocks.analysis.MultiInPredicate;
import com.starrocks.analysis.NullLiteral;
import com.starrocks.analysis.ParseNode;
import com.starrocks.analysis.SlotRef;
import com.starrocks.analysis.StringLiteral;
import com.starrocks.analysis.SubfieldExpr;
import com.starrocks.analysis.Subquery;
import com.starrocks.analysis.TimestampArithmeticExpr;
import com.starrocks.analysis.VarBinaryLiteral;
import com.starrocks.analysis.VirtualSlotRef;
import com.starrocks.sql.ast.ArrayExpr;
import com.starrocks.sql.ast.CTERelation;
import com.starrocks.sql.ast.ExceptRelation;
import com.starrocks.sql.ast.FieldReference;
import com.starrocks.sql.ast.IntersectRelation;
import com.starrocks.sql.ast.IntervalLiteral;
import com.starrocks.sql.ast.JoinRelation;
import com.starrocks.sql.ast.MapExpr;
import com.starrocks.sql.ast.PivotRelation;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.SubqueryRelation;
import com.starrocks.sql.ast.TableRelation;
import com.starrocks.sql.ast.UnionRelation;
import com.starrocks.sql.ast.ValuesRelation;
import com.starrocks.sql.ast.ViewRelation;

import java.util.Set;

/**
 * Hardcoded whitelist of AST node types allowed inside a VALIDATE inner query.
 *
 * <p>The set covers only read-only, access-safe query shapes: query/relation nodes and scalar/predicate expression
 * nodes (plus analyzer-introduced benign nodes such as {@link CastExpr}, {@link CloneExpr}, {@link FieldReference}).
 *
 * <p>Membership is checked by <em>exact</em> {@link Class} identity ({@code ALLOWED.contains(node.getClass())}), not
 * {@code isInstance}: a subclass of an allowed type is a distinct {@code Class} key and therefore does <em>not</em>
 * match. This is a deliberate security property — an unknown subclass of, say, {@code SlotRef} must be listed
 * explicitly to be allowed, so the base classes are expanded into their concrete leaves below. Anything not listed —
 * table-function relations (e.g. {@code files()}), lambdas, dictionary/variable expressions, DDL/DML statements — is
 * reported as a violation. Start conservative; widen deliberately with tests.
 */
public final class ValidateAllowedNodes {
    private static final Set<Class<?>> ALLOWED = ImmutableSet.of(
            // ---- query / relation nodes ----
            QueryStatement.class,
            SelectRelation.class,
            TableRelation.class,
            JoinRelation.class,
            SubqueryRelation.class,
            CTERelation.class,
            ValuesRelation.class,
            UnionRelation.class,
            ExceptRelation.class,
            IntersectRelation.class,
            ViewRelation.class,
            PivotRelation.class,
            // ---- references ----
            SlotRef.class,
            VirtualSlotRef.class,
            FieldReference.class,
            // ---- literals (concrete subclasses of LiteralExpr) ----
            StringLiteral.class,
            LargeStringLiteral.class,
            IntLiteral.class,
            BoolLiteral.class,
            LargeIntLiteral.class,
            DecimalLiteral.class,
            FloatLiteral.class,
            DateLiteral.class,
            NullLiteral.class,
            VarBinaryLiteral.class,
            MaxLiteral.class,
            IntervalLiteral.class,
            // ---- predicates (concrete subclasses of Predicate) ----
            BinaryPredicate.class,
            CompoundPredicate.class,
            InPredicate.class,
            LargeInPredicate.class,
            MultiInPredicate.class,
            IsNullPredicate.class,
            LikePredicate.class,
            BetweenPredicate.class,
            ExistsPredicate.class,
            // ---- function calls (name whitelisted separately; DictQueryExpr excluded on purpose) ----
            FunctionCallExpr.class,
            GroupingFunctionCallExpr.class,
            // ---- other scalar expressions ----
            ArithmeticExpr.class,
            TimestampArithmeticExpr.class,
            AnalyticExpr.class,
            CaseExpr.class,
            CastExpr.class,
            CloneExpr.class,
            ArrayExpr.class,
            MapExpr.class,
            CollectionElementExpr.class,
            ArraySliceExpr.class,
            SubfieldExpr.class,
            ArrowExpr.class,
            Subquery.class);

    private ValidateAllowedNodes() {
    }

    public static boolean isAllowed(ParseNode node) {
        return node != null && ALLOWED.contains(node.getClass());
    }
}
