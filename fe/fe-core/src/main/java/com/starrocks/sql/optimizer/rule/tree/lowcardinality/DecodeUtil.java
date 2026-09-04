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
import com.google.common.collect.Lists;
import com.starrocks.catalog.ArrayType;
import com.starrocks.catalog.Type;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.DictMappingOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;

import java.util.List;

public final class DecodeUtil {

    private DecodeUtil() {}

    static Type getDictifiedType(Type type) {
        if (type == null) {
            return null;
        }
        Preconditions.checkState(!type.isStructType());
        if (type.isStringType()) {
            return Type.INT;
        }
        if (type.isStringArrayType()) {
            return ArrayType.ARRAY_INT;
        }
        return type;
    }

    static LambdaFunctionOperator getLambdaFunctionArg(CallOperator call) {
        if (call.getChild(0) instanceof LambdaFunctionOperator lambda) {
            return lambda;
        }
        if (call.getChild(call.getChildren().size() - 1) instanceof LambdaFunctionOperator lambda) {
            return lambda;
        }
        return null;
    }

    static boolean isLambdaColumn(ColumnRefOperator column) {
        return column.getOpType().equals(OperatorType.LAMBDA_ARGUMENT);
    }

    public static List<ColumnRefOperator> collectAllColumnRefs(ScalarOperator operator) {
        List<ColumnRefOperator> columns = Lists.newArrayList();
        collectAllColumnRefs(operator, columns);
        return columns;
    }

    private static void collectAllColumnRefs(ScalarOperator operator, List<ColumnRefOperator> columns) {
        if (operator instanceof ColumnRefOperator ref) {
            columns.add(ref);
            return;
        }
        if (operator instanceof DictMappingOperator dictMapping) {
            collectAllColumnRefs(dictMapping.getDictColumn(), columns);
            collectAllColumnRefs(dictMapping.getStringProvideOperator() != null
                    ? dictMapping.getStringProvideOperator() : dictMapping.getOriginScalaOperator(), columns);
            return;
        }
        for (ScalarOperator child : operator.getChildren()) {
            collectAllColumnRefs(child, columns);
        }
    }
}