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

package com.starrocks.sql.optimizer.rule.tree.lowcardinality;

import com.google.common.base.Preconditions;
import com.starrocks.sql.optimizer.base.ColumnRefSet;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rewrite.BaseScalarOperatorShuttle;
import com.starrocks.type.ArrayType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.Type;

import java.util.Map;
import java.util.Optional;

final class DecodeUtil {

    private DecodeUtil() {}

    static Type getDictifiedType(Type type) {
        if (type == null) {
            return null;
        }
        Preconditions.checkState(!type.isStructType());
        if (type.isStringType()) {
            return IntegerType.INT;
        }
        if (type.isStringArrayType()) {
            return ArrayType.ARRAY_INT;
        }
        return type;
    }

    // For structs, we only return the encoded fields collected by DecodeCollector.
    static ColumnRefSet getUsedColumns(ScalarOperator scalarOperator, DecodeContext context) {
        if (scalarOperator.isColumnRef()) {
            ColumnRefOperator c = scalarOperator.cast();
            // DO NOT SUBMIT
            return c.getOpType().equals(OperatorType.LAMBDA_ARGUMENT) ? ColumnRefSet.of() : ColumnRefSet.of(c);
        }
        Map<String, ColumnRefOperator> structFieldsData = context.getFieldUseStringRefMap(scalarOperator);
        if (structFieldsData != null) {
            return new ColumnRefSet(structFieldsData.values());
        }
        ColumnRefSet result = new ColumnRefSet();
        scalarOperator.getChildren().forEach(c -> result.union(getUsedColumns(c, context)));
        return result;
    }

    static class ExprReplacer extends BaseScalarOperatorShuttle {
        private final Map<ScalarOperator, ScalarOperator> exprMapping;
        private final ColumnRefSet supportColumns;
        private final DecodeContext context;

        public ExprReplacer(Map<ScalarOperator, ScalarOperator> exprMapping,
                            ColumnRefSet supportColumns,
                            DecodeContext context) {
            this.exprMapping = exprMapping;
            this.supportColumns = supportColumns;
            this.context = context;
        }

        @Override
        public Optional<ScalarOperator> preprocess(ScalarOperator scalarOperator) {
            if (exprMapping.containsKey(scalarOperator)
                    && supportColumns.containsAll(getUsedColumns(scalarOperator, context))) {
                return Optional.of(exprMapping.get(scalarOperator));
            }
            return Optional.empty();
        }
    }
}