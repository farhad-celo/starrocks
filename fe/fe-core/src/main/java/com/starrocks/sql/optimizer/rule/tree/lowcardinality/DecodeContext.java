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
import com.google.common.collect.Maps;
import com.starrocks.catalog.AggregateFunction;
import com.starrocks.catalog.Function;
import com.starrocks.catalog.FunctionName;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.sql.ast.expression.ExprUtils;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.base.ColumnRefSet;
import com.starrocks.sql.optimizer.operator.Operator;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.CollectionElementOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.DictMappingOperator;
import com.starrocks.sql.optimizer.operator.scalar.IsNullPredicateOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.operator.scalar.SubfieldOperator;
import com.starrocks.sql.optimizer.rewrite.BaseScalarOperatorShuttle;
import com.starrocks.sql.optimizer.statistics.ColumnDict;
import com.starrocks.thrift.TFunctionBinaryType;
import com.starrocks.type.ArrayType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.StructField;
import com.starrocks.type.StructType;
import com.starrocks.type.Type;
import com.starrocks.type.VarcharType;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;

import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeCollector.LOW_CARD_ARRAY_FUNCTIONS;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeCollector.LOW_CARD_MULTI_INPUT_ARRAY_FUNCTIONS;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeCollector.LOW_CARD_STRUCT_FUNCTIONS;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeCollector.supportLowCardinality;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeUtil.getDictifiedType;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeUtil.getLambdaFunctionArg;

/*
 * DecodeContext is used to store the information needed for decoding
 * 1. Record the original string column information(Expressions, Define) used for low cardinality
 * 2. Record the global dictionary
 * 3. Generate new dictionary column:
 *  a. Generate corresponding dictionary ref based on the string ref
 *  b. Generate the define expression of global dictionary column
 *  c. Rewrite string expressions
 *  d. Rewrite string aggregate expressions
 *  e. Generate the global dictionary expression
 */
class DecodeContext {
    private final ColumnRefFactory factory;

    // Global DictCache
    // string ColumnRefId -> ColumnDict
    Map<Integer, ColumnDict> stringRefToDicts = Maps.newHashMap();

    // all support dict string columns
    Set<Integer> allStringColumns = new LinkedHashSet<>();

    // string column -> define expression
    // e.g.
    // Project Node:
    // | a : upper(c)
    // | b : b
    // a is string column, upper(b) is his define
    // b is string column, b is his define
    Map<Integer, ScalarOperator> stringRefToDefineExprMap = Maps.newHashMap();

    // string column -> all relation expression
    // e.g.
    // select upper(a) from t0 where lower(a) = 'tt'
    // a is string column
    // upper(a), lower(a) is relation expression
    Set<ScalarOperator> stringExpressions;

    // all string aggregate expressions
    Map<Integer, List<CallOperator>> stringAggregateExprs = Maps.newHashMap();

    Map<Integer, ColumnRefSet> aggIdToSupportColumns = Maps.newHashMap();

    Map<ScalarOperator, ColumnRefSet> stringExprToSupportColumns = Maps.newIdentityHashMap();

    // The string columns used by the operator
    // IdentityHashMap: use object == object, operator equals is not enough
    Map<Operator, DecodeInfo> operatorDecodeInfo = Maps.newIdentityHashMap();

    // global dict expressions
    Map<Integer, ScalarOperator> globalDictsExpr = Maps.newHashMap();

    Map<ColumnRefOperator, ColumnRefOperator> stringRefToDictRefMap = Maps.newHashMap();

    Map<ColumnRefOperator, ScalarOperator> dictRefToDefineExprMap = Maps.newHashMap();

    Map<ScalarOperator, ScalarOperator> stringExprToDictExprMap = Maps.newIdentityHashMap();

    Map<ScalarOperator, ScalarOperator> stringExprToDictDefineExprMap = Maps.newIdentityHashMap();

    StructManager structManager;

    UnionDictionaryManager unionDictionaryManager;

    Map<ScalarOperator, ColumnRefOperator> arrayMapPseudoColumns;

    DecodeContext(ColumnRefFactory factory) {
        this.factory = factory;
    }

    public void initRewriteExpressions() {
        rewriteStringRefToDictRef();
        rewriteGlobalDict();
        rewriteStringExpressions();
        rewriteStringAggregations();
    }

    ColumnRefOperator getUseStringRef(ScalarOperator operator) {
        if (operator.isColumnRef()) {
            return (ColumnRefOperator) operator;
        }
        if (operator instanceof SubfieldOperator) {
            SubfieldOperator subfieldOperator = operator.cast();
            Map<String, ColumnRefOperator> fieldsUseRefMap = structManager.getFieldStringRefMap(
                    subfieldOperator.getChild(0));
            Preconditions.checkNotNull(fieldsUseRefMap);
            Preconditions.checkState(subfieldOperator.getFieldNames().size() == 1
                    && fieldsUseRefMap.containsKey(subfieldOperator.getFieldNames().get(0)));
            return getUseStringRef(fieldsUseRefMap.get(subfieldOperator.getFieldNames().get(0)));
        }
        if (operator instanceof CallOperator call && FunctionSet.ARRAY_MAP.equals(call.getFnName())) {
            return arrayMapPseudoColumns.get(call);
        }
        if (operator instanceof CallOperator call && (FunctionSet.ARRAY_AGG.equals(call.getFnName())
                    || LOW_CARD_MULTI_INPUT_ARRAY_FUNCTIONS.contains(call.getFnName()))) {
            return getUseStringRef(operator.getChild(0));
        }

        List<ColumnRefOperator> columnRefs = Lists.newArrayList();
        for (ScalarOperator child : operator.getChildren()) {
            ColumnRefOperator ref = getUseStringRef(child);
            if (ref != null) {
                columnRefs.add(ref);
            }
        }
        if (columnRefs.isEmpty()) {
            return null;
        }
        Preconditions.checkState(columnRefs.stream().distinct().count() == 1);
        return columnRefs.get(0);
    }

    private void rewriteStringRefToDictRef() {
        // rewrite string column to dict column
        for (Integer stringId : allStringColumns) {
            if (!stringRefToDefineExprMap.containsKey(stringId)) {
                continue;
            }
            ColumnRefOperator stringRef = factory.getColumnRef(stringId);
            ColumnRefOperator dictRef = createNewDictColumn(stringRef);
            stringRefToDictRefMap.put(stringRef, dictRef);
        }

        // rewrite dict column define and decode expressions
        for (Integer stringId : stringRefToDefineExprMap.keySet()) {
            ScalarOperator stringDefineExpr = stringRefToDefineExprMap.get(stringId);
            ColumnRefOperator stringRef = factory.getColumnRef(stringId);
            ColumnRefOperator dictRef = stringRefToDictRefMap.get(stringRef);
            stringExprToDictExprMap.put(stringRef, decode(stringRef));
            // return type is dict
            ScalarOperator dictExpr = define(stringDefineExpr);
            dictRefToDefineExprMap.put(dictRef, dictExpr);
        }
    }

    private boolean isNullSensitiveToRef(ScalarOperator op, int refId) {
        if (!op.getUsedColumns().contains(refId)) {
            return false;
        }
        if (op instanceof IsNullPredicateOperator) {
            return true;
        }
        if (op instanceof CallOperator) {
            String fn = ((CallOperator) op).getFnName();
            if (FunctionSet.IFNULL.equalsIgnoreCase(fn) || FunctionSet.COALESCE.equalsIgnoreCase(fn)
                    || FunctionSet.NULLIF.equalsIgnoreCase(fn) || FunctionSet.CONCAT_WS.equalsIgnoreCase(fn)) {
                return true;
            }
        }
        for (ScalarOperator child : op.getChildren()) {
            if (isNullSensitiveToRef(child, refId)) {
                return true;
            }
        }
        return false;
    }

    private void rewriteGlobalDict() {
        GlobalDictRewriter dictRewriter = new GlobalDictRewriter();
        for (Integer stringId : stringRefToDefineExprMap.keySet()) {
            if (stringRefToDicts.containsKey(stringId)) {
                continue;
            }

            // rewrite global dict expression
            // rewrite to dictMapping(originDictColumn, fold_expression)
            // e.g. A : upper(B)
            //      B : lower(C)
            // decode A: dictMapping(B, upper(B)) -> dictMapping(C, upper(lower(C)))
            ColumnRefOperator dictRef = stringRefToDictRefMap.get(factory.getColumnRef(stringId));
            if (dictRef.getType().isStructType()) {
                continue;
            }
            ScalarOperator stringDefineExpr = stringRefToDefineExprMap.get(stringId);

            // Check before flattening: if a NULL-sensitive expression is built on a DERIVED dict
            // (one defined by another expression, not a base column), do NOT flatten it through the
            // child define. Keep it referencing the intermediate dict directly, so producer and
            // consumer build the dictionary the same way (a derived dict carries a synthetic NULL
            // code that flattening would drop).
            ColumnRefOperator keepUseRef = getUseStringRef(stringDefineExpr);
            if (keepUseRef != null && !stringRefToDicts.containsKey(keepUseRef.getId())
                    && stringRefToDefineExprMap.containsKey(keepUseRef.getId())
                    && isNullSensitiveToRef(stringDefineExpr, keepUseRef.getId())) {
                ColumnRefOperator keepDictRef = stringRefToDictRefMap.get(keepUseRef);
                globalDictsExpr.put(dictRef.getId(),
                        new DictMappingOperator(keepDictRef, stringDefineExpr.clone(), dictRef.getType()));
                continue;
            }

            ScalarOperator defineExpr = stringDefineExpr.accept(dictRewriter, null);
            List<ColumnRefOperator> defineUsedStringRef = defineExpr.getColumnRefs();
            Preconditions.checkState(defineUsedStringRef.stream().distinct().count() == 1);

            ColumnRefOperator defineUsedDictRef = stringRefToDictRefMap.get(defineUsedStringRef.get(0));
            ScalarOperator globalDictExpr = new DictMappingOperator(defineUsedDictRef, defineExpr, dictRef.getType());
            globalDictsExpr.put(dictRef.getId(), globalDictExpr);
        }
    }

    private void rewriteStringExpressions() {
        // rewrite string expression
        for (ScalarOperator stringExpr : stringExpressions) {
            decode(stringExpr);
        }
    }

    private void rewriteStringAggregations() {
        // rewrite string aggregate expression
        stringAggregateExprs.forEach((aggId, aggFns) -> {
            ColumnRefSet supportColumns = aggIdToSupportColumns.get(aggId);
            Preconditions.checkNotNull(supportColumns);
            AggregateRewriter rewriter = new AggregateRewriter(aggId, supportColumns);
            for (CallOperator aggFn : aggFns) {
                CallOperator new1stAggFn = (CallOperator) (aggFn.accept(rewriter, null));
                stringExprToDictExprMap.put(aggFn, new1stAggFn);
            }
        });
    }

    // create a new dictionary column and assign the same property except for the type and column id
    // the input column maybe a dictionary column or a string column
    private ColumnRefOperator createNewDictColumn(ColumnRefOperator column) {
        if (column.getType().isStringArrayType()) {
            return factory.create(column.getName(), ArrayType.ARRAY_INT, column.isNullable(),
                    column.getOpType().equals(OperatorType.LAMBDA_ARGUMENT));
        } else if (column.getType().isStringType()) {
            return factory.create(column.getName(), IntegerType.INT, column.isNullable(),
                    column.getOpType().equals(OperatorType.LAMBDA_ARGUMENT));
        } else if (column.getType().isStructType()) {
            Map<String, ColumnRefOperator> fieldsData = structManager.getFieldStringRefMap(column);
            Preconditions.checkNotNull(fieldsData);
            List<StructField> structFields = Lists.newArrayList();
            StructType type = (StructType) column.getType();
            for (int i = 0; i < type.getFields().size(); ++i) {
                if (fieldsData.containsKey(type.getField(i).getName())) {
                    // DecodeCollector ensures all ColumnRefOperators in this map have STRING or ARRAY<STRING> type.
                    Preconditions.checkState(type.getField(i).getType().isStringArrayType()
                            || type.getField(i).getType().isStringType());
                    structFields.add(new StructField(type.getField(i).getName(),
                            type.getField(i).getType().isArrayType() ? ArrayType.ARRAY_INT : IntegerType.INT));
                } else {
                    structFields.add(type.getField(i));
                }
            }
            StructType dictType = new StructType(structFields, type.isNamed());
            return factory.create(column.getName(), dictType, column.isNullable(),
                    column.getOpType().equals(OperatorType.LAMBDA_ARGUMENT));
        } else {
            throw new IllegalArgumentException("Unsupported dictified type: " +  column.getType());
        }
    }

    private static final String DICT_ENCODE = "dict_encode";
    private static final Function DICT_ENCODE_FN = new Function(
            30700, new FunctionName(DICT_ENCODE), List.of(VarcharType.VARCHAR, IntegerType.INT), IntegerType.INT, false);
    static {
        DICT_ENCODE_FN.setBinaryType(TFunctionBinaryType.BUILTIN);
    }

    private static ScalarOperator dictEncodeConstant(ScalarOperator constant, int dictId) {
        if (!constant.getType().isStringType() && !constant.getType().isStringArrayType()) {
            return constant;
        }
        ConstantOperator dictSlotConstant = ConstantOperator.createInt(dictId);
        if (constant instanceof ConstantOperator constantOp) {
            if (constantOp.getType().isStringType()) {
                return new CallOperator(
                        DICT_ENCODE, IntegerType.INT, List.of(constant, dictSlotConstant), DICT_ENCODE_FN);
            }
            Preconditions.checkState(constantOp.isNull());
            return ConstantOperator.createNull(ArrayType.ARRAY_INT);
        }
        if (constant instanceof ArrayOperator arrayOp) {
            Preconditions.checkState(arrayOp.getChildren().stream().allMatch(ScalarOperator::isConstantRef));
            return new ArrayOperator(ArrayType.ARRAY_INT, arrayOp.isNullable(),
                    arrayOp.getChildren().stream().map(k -> (ScalarOperator) new CallOperator(
                                    DICT_ENCODE, IntegerType.INT, List.of(k, dictSlotConstant), DICT_ENCODE_FN))
                            .collect(Collectors.toCollection(ArrayList::new)));
        }
        throw new IllegalArgumentException("Invalid constant argument for array function: " + constant);

    }

    private static CallOperator buildCallOperator(CallOperator call, List<ScalarOperator> args) {
        String fnName = call.getFnName();
        final Function fn;
        if (fnName.equals(FunctionSet.NAMED_STRUCT)) {
            Type[] argTypes = args.stream().map(ScalarOperator::getType).toArray(Type[]::new);
            fn = ExprUtils.getBuiltinFunction(fnName, argTypes, Function.CompareMode.IS_SUPERTYPE_OF).copy();
            List<StructField> fields = Lists.newArrayList();
            for (int i = 0; i < args.size(); i += 2) {
                fields.add(new StructField(((ConstantOperator) args.get(i)).getVarchar(), argTypes[i + 1]));
            }
            fn.setRetType(new StructType(fields, true));
        } else if (fnName.equals(FunctionSet.ARRAY_MAP)) {
            if (args.get(args.size() - 1) instanceof LambdaFunctionOperator) {
                ScalarOperator lambda = args.get(args.size() - 1);
                List<ScalarOperator> newArgs = Lists.newArrayList();
                newArgs.add(lambda);
                newArgs.addAll(args.subList(0, args.size() - 1));
                args = newArgs;
            }
            Type[] argTypes = args.stream().map(ScalarOperator::getType).toArray(Type[]::new);
            fn = ExprUtils.getBuiltinFunction(
                    fnName, argTypes, Function.CompareMode.IS_NONSTRICT_SUPERTYPE_OF).copy();
            // DO NOT SUBMIT
            fn.setRetType(new ArrayType(((LambdaFunctionOperator) args.get(0)).getLambdaExpr().getType()));
        } else {
            Type[] argTypes = args.stream().map(ScalarOperator::getType).toArray(Type[]::new);
            fn = ExprUtils.getBuiltinFunction(fnName, argTypes, Function.CompareMode.IS_SUPERTYPE_OF);
        }
        return new CallOperator(fnName, fn.getReturnType(), args, fn,
                call.isDistinct(), call.isRemovedDistinct());
    }


    // Decodes a dictified struct by generating a new named_struct function and applying decode on all dictified fields.
    private ScalarOperator decodeStruct(ScalarOperator dictExpression,
                                       ScalarOperator expression) {
        Preconditions.checkState(dictExpression.getType().isStructType());
        StructType exprType =  (StructType) expression.getType();
        StructType dictType = (StructType) dictExpression.getType();
        List<ScalarOperator> newFields = Lists.newArrayList();
        Map<String, ColumnRefOperator> fieldsStringRefMap = structManager.getFieldStringRefMap(expression);
        Preconditions.checkNotNull(fieldsStringRefMap);
        for (int i = 0; i < exprType.getFields().size(); ++i) {
            String fieldName = exprType.getField(i).getName();
            newFields.add(ConstantOperator.createVarchar(fieldName));
            Type fieldOriginalType =  exprType.getField(i).getType();
            Type fieldDictType = dictType.getField(i).getType();
            ScalarOperator fieldExpr =  new SubfieldOperator(dictExpression, fieldDictType, List.of(fieldName));
            if (fieldOriginalType.matchesType(fieldDictType)) {
                newFields.add(fieldExpr);
            } else {
                ColumnRefOperator fieldStringRef = fieldsStringRefMap.get(fieldName);
                Preconditions.checkNotNull(fieldStringRef);
                ColumnRefOperator fieldDictRef = stringRefToDictRefMap.get(fieldStringRef);
                Preconditions.checkNotNull(fieldDictRef);
                newFields.add(new DictMappingOperator(
                        fieldOriginalType,
                        fieldDictRef,
                        new ColumnRefOperator(
                                fieldDictRef.getId(),
                                fieldExpr.getType(),
                                fieldDictRef.getName(),
                                fieldExpr.isNullable()),
                        fieldExpr));
            }
        }
        Type[] argTypes = newFields.stream().map(ScalarOperator::getType).toArray(Type[]::new);
        Function fn = ExprUtils.getBuiltinFunction(
                FunctionSet.NAMED_STRUCT, argTypes, Function.CompareMode.IS_SUPERTYPE_OF).copy();
        fn.setRetType(exprType);
        return new CallOperator(FunctionSet.NAMED_STRUCT, fn.getReturnType(), newFields, fn);
    }

    private ScalarOperator decodeImpl(ScalarOperator expression) {
        ColumnRefSet supportColumns = stringExprToSupportColumns.get(expression);
        DictExprEncoder encoder = new DictExprEncoder(supportColumns);
        ScalarOperator result = expression.accept(encoder, null);
        if (result.getType().isStructType()) {
            Preconditions.checkState(encoder.getAnchorOp() == null);
            return decodeStruct(result, expression);
        } else if (result instanceof SubfieldOperator subfieldOperator) {
            Preconditions.checkState(expression instanceof SubfieldOperator);
            if (subfieldOperator.getFieldNames().size() != 1 ||
                    !structManager.getFieldStringRefMap(expression.getChild(0))
                            .containsKey(subfieldOperator.getFieldNames().get(0))) {
                // Getting non dictified field from a struct
                return result;
            }
        }
        ColumnRefOperator useStringRef = getUseStringRef(expression);
        if (useStringRef == null || (supportColumns != null && !supportColumns.contains(useStringRef))) {
            return result;
        }
        ColumnRefOperator useDictRef = stringRefToDictRefMap.get(useStringRef);
        Preconditions.checkNotNull(useDictRef);
        if (useStringRef.getType().isVarchar() && encoder.getAnchorOp() == null) {
            return new DictMappingOperator(useDictRef, expression.clone(), expression.getType());
        } else if (result.isColumnRef() && encoder.getAnchorOp() == null) {
            // decode array-column-ref
            return new DictMappingOperator(useDictRef, result, expression.getType());
        } else if (result instanceof CallOperator
                && LOW_CARD_ARRAY_FUNCTIONS.contains(((CallOperator) result).getFnName())
                && !supportLowCardinality(expression.getType())) {
            Preconditions.checkState(encoder.getAnchorOp() == null);
            return result;
        }
        result = encoder.processAnchor(result, expression);
        return new DictMappingOperator(expression.getType(), useDictRef, result, encoder.getAnchorOp());
    }

    // returns a dictified form of the expression as a new expression in string domain. Uses stringExprToDictExprMap
    // as a cache.
    private ScalarOperator decode(ScalarOperator expression) {
        if (stringExprToDictExprMap.containsKey(expression)) {
            return stringExprToDictExprMap.get(expression);
        }
        ScalarOperator decodeExpr = decodeImpl(expression);
        stringExprToDictExprMap.put(expression, decodeExpr);
        return decodeExpr;
    }

    public ScalarOperator defineImpl(ScalarOperator expression) {
        DictExprEncoder encoder = new DictExprEncoder(stringExprToSupportColumns.get(expression));
        ScalarOperator result = expression.accept(encoder, null);
        if (expression.getType().isStructType()) {
            return result;
        }
        ColumnRefOperator useStringRef = getUseStringRef(expression);
        ColumnRefOperator useDictRef = stringRefToDictRefMap.get(useStringRef);
        if (useStringRef.getType().isVarchar() && encoder.getAnchorOp() == null) {
            return new DictMappingOperator(useDictRef, expression.clone(), useDictRef.getType());

        }
        if (encoder.getAnchorOp() != null) {
            if (!result.isColumnRef()) {
                // e.g. upper(array_column[0])), need define string-expr by dict-expr
                return new DictMappingOperator(
                        IntegerType.INT, useDictRef, result, encoder.getAnchorOp());
            } else {
                // e.g. array_column[0], need define to string
                return encoder.getAnchorOp();
            }
        }

        return result;
    }


    // returns a dictified form of the expression as a new expression in dict domain. Uses stringExprToDictDefineExprMap
    // as a cache.
    public ScalarOperator define(ScalarOperator expression) {
        if (stringExprToDictDefineExprMap.containsKey(expression)) {
            return stringExprToDictDefineExprMap.get(expression);
        }
        ScalarOperator define = defineImpl(expression);
        stringExprToDictDefineExprMap.put(expression, define);
        return define;
    }

    // Rewrites the input expr into its dict form by replacing any expression in stringExpressions to its decoded dict
    // format. For ColumnRefOperators, only column refs in supportColumns will be processed and the rest will be left
    // untouched.
    public ScalarOperator rewrite(ScalarOperator expr, ColumnRefSet supportColumns) {
         class ExprRewriter extends BaseScalarOperatorShuttle {
            final ColumnRefSet supportColumns;

            ExprRewriter(ColumnRefSet supportColumns) {
                Preconditions.checkNotNull(supportColumns);
                this.supportColumns = supportColumns;
            }

            @Override
            public Optional<ScalarOperator> preprocess(ScalarOperator expr) {
                if (expr instanceof ColumnRefOperator ref) {
                    return Optional.of(supportColumns.contains(ref) ? decode(ref) : ref);
                }
                if (stringExpressions.contains(expr)) {
                    return Optional.of(decode(expr));
                }
                return Optional.empty();
            }
        }
        ExprRewriter rewriter = new ExprRewriter(supportColumns);
        return expr.accept(rewriter, null);
    }

    // Returns define(expr) if expr exists in stringExpressions, otherwise returns rewrite(expr, supportColumns)
    ScalarOperator defineOrRewrite(ScalarOperator expr, ColumnRefSet supportColumns) {
        return stringExpressions.contains(expr) ? define(expr) : rewrite(expr, supportColumns);
    }



    // Returns a half backed form of encoded a string-typed scalar expression into its dict-encoded form.
    // String column refs become dict refs (via stringRefToDictRefMap) and supported array/struct ops are rewritten to
    // operate on codes (int / array<int>) instead of strings. Only columns in supportColumns are encoded
    // others pass through untouched.
    // For expressions that take an array/struct column into scalar form, anchorOp marks location in which
    // array/struct -> string transformation happens and the returning scalar operator contains a mock string ref
    // instead of this transformation.
    // The result must always be processed in define() or decode() functions.
    private class DictExprEncoder extends BaseScalarOperatorShuttle {
        // to mark special array expression: array_min/array_max/array[x]
        // their return type is string, but use low cardinality optimization, we need execute them first
        private ScalarOperator anchorOp;
        private final ColumnRefSet supportColumns;

        public DictExprEncoder(ColumnRefSet supportColumns) {
            this.supportColumns = supportColumns;
        }

        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void context) {
            return stringRefToDictRefMap.getOrDefault(variable, variable);
        }

        @Override
        public ScalarOperator visitCall(CallOperator call, Void context) {
            if (!isSupportedArrayFunction(call)
                    && !LOW_CARD_STRUCT_FUNCTIONS.contains(call.getFnName())) {
                return super.visitCall(call, context);
            }
            if (call.getFnName().equalsIgnoreCase(FunctionSet.ARRAY_MAP)) {
                LambdaFunctionOperator lambdaFunction = getLambdaFunctionArg(call);
                List<ScalarOperator> arrayChildren = call.getChild(0) == lambdaFunction ?
                        call.getChildren().subList(1, call.getChildren().size()) :
                        call.getChildren().subList(0, call.getChildren().size() - 1);
                List<ScalarOperator> newChildren = arrayChildren.stream()
                        .map(c -> defineOrRewrite(c, supportColumns))
                        .collect(Collectors.toCollection(ArrayList::new));
                List<ColumnRefOperator> newLambdaColumns = lambdaFunction.getRefColumns().stream()
                        .map(c -> supportColumns.contains(c) ? stringRefToDictRefMap.get(c) : c)
                        .toList();
                ScalarOperator newLambdaExpr = arrayMapPseudoColumns.containsKey(call) ?
                        define(lambdaFunction.getLambdaExpr()) :
                        rewrite(lambdaFunction.getLambdaExpr(), supportColumns);
                newChildren.add(new LambdaFunctionOperator(
                        newLambdaColumns, newLambdaExpr, lambdaFunction.getType()));
                boolean update = newLambdaExpr != lambdaFunction;
                for (int i = 0; i < arrayChildren.size(); ++i) {
                    update = arrayChildren.get(i) != newChildren.get(i);
                }
                return update ? buildCallOperator(call, newChildren) : call;
            }
            boolean[] hasChange = new boolean[1];
            List<ScalarOperator> newChildren;
            if (!LOW_CARD_MULTI_INPUT_ARRAY_FUNCTIONS.contains(call.getFnName())) {
                newChildren = visitList(call.getChildren(), hasChange);
            } else {
                newChildren = Lists.newArrayList();
                newChildren.add(defineOrRewrite(call.getChild(0), supportColumns));
                final boolean processinSortArgs = call.getFnName().equals(FunctionSet.ARRAY_SORTBY);
                for (int i = 1; i < call.getChildren().size(); ++i) {
                    newChildren.add(processinSortArgs ? defineOrRewrite(call.getChild(i), supportColumns) :
                            rewrite(call.getChild(i), supportColumns));
                }
                for (int i = 0; i < newChildren.size(); ++i) {
                    hasChange[0] = hasChange[0] || newChildren.get(i) != call.getChild(i);
                }
            }
            if (!hasChange[0]) {
                return call;
            }

            if (isSupportedSingleInputArrayFunction(call)) {
                ColumnRefOperator stringRef = getUseStringRef(call);
                ColumnRefOperator dictRef = stringRefToDictRefMap.get(stringRef);
                Preconditions.checkNotNull(dictRef);
                newChildren = newChildren.stream().map(op -> op.isConstant() ?
                        dictEncodeConstant(op, dictRef.getId()) : op).collect(Collectors.toCollection(ArrayList::new));
            }
            ScalarOperator result = buildCallOperator(call, newChildren);

            if (call.getType().isStringType()) {
                return processAnchor(result, call);
            }
            return result;
        }

        @Override
        public ScalarOperator visitCollectionElement(CollectionElementOperator collectionElementOp, Void context) {
            boolean[] hasChange = new boolean[1];
            List<ScalarOperator> newChildren = visitList(collectionElementOp.getChildren(), hasChange);
            if (!hasChange[0]) {
                return collectionElementOp;
            }
            Preconditions.checkState(newChildren.get(0).getType().isArrayType());
            ScalarOperator result = new CollectionElementOperator(((ArrayType) newChildren.get(0)
                    .getType()).getItemType(), newChildren.get(0), newChildren.get(1), collectionElementOp.isCheckOutOfBounds());

            return processAnchor(result, collectionElementOp);
        }

        @Override
        public ScalarOperator visitSubfield(SubfieldOperator operator, Void context) {
            ScalarOperator newChild = operator.getChild(0).accept(this, context);
            if (newChild == operator.getChild(0)) {
                return operator;
            }
            StructType originalType = (StructType) operator.getChild(0).getType();
            StructType newType = (StructType) newChild.getType();
            ScalarOperator result = new SubfieldOperator(
                    newChild,
                    newType.getField(operator.getFieldNames().get(0)).getType(),
                    operator.getFieldNames(),
                    operator.getCopyFlag());
            if (!originalType.getField(operator.getFieldNames().get(0)).getType().matchesType(
                    newType.getField(operator.getFieldNames().get(0)).getType())) {
                Map<String, ColumnRefOperator> useFieldRefMap = structManager.getFieldStringRefMap(
                        operator.getChild(0));
                Preconditions.checkNotNull(useFieldRefMap);
                ColumnRefOperator fieldUseStringRef = useFieldRefMap.get(operator.getFieldNames().get(0));
                Preconditions.checkNotNull(fieldUseStringRef);
                ColumnRefOperator childDictRef = stringRefToDictRefMap.get(fieldUseStringRef);
                Preconditions.checkNotNull(childDictRef);
                if (operator.getType().isStringType()) {
                    return processAnchor(result, operator);
                } else {
                    return result;
                }
            }
            return result;
        }

        private ScalarOperator processAnchor(ScalarOperator expr, ScalarOperator originalOperator) {
            if (anchorOp != null && !anchorOp.equals(expr)) {
                return expr;
            }

            // e.g. DictExpr(useDictColumn, array_distinct(array_column)[0])
            // we need compute array_distinct(x)[0] first, then decode to string on the result
            anchorOp = expr;
            // mock use column ref, only type is used, ScalarOperatorToExpr will rewrite it
            // @todo: rewrite ScalarOperatorToExpr process when v1 is deprecated
            ColumnRefOperator anchorUseStringRef = getUseStringRef(originalOperator);
            ColumnRefOperator anchorUseDictRef = stringRefToDictRefMap.get(anchorUseStringRef);
            Preconditions.checkNotNull(anchorUseDictRef);

            return new ColumnRefOperator(anchorUseDictRef.getId(), expr.getType(),
                    anchorUseDictRef.getName(), anchorUseDictRef.isNullable());
        }

        ScalarOperator getAnchorOp() {
            return anchorOp;
        }
    }

    private class AggregateRewriter extends BaseScalarOperatorShuttle {
        private final int aggId;
        private final ColumnRefSet supportColumns;
        private AggregateFunction newFn;
        private AggregateFunction originalFn;

        public AggregateRewriter(int aggId, ColumnRefSet supportColumns) {
            this.aggId = aggId;
            this.supportColumns = supportColumns;
        }

        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void ignore) {
            if (variable.getId() == aggId) {
                Preconditions.checkNotNull(newFn);
                ColumnRefOperator dictRef = stringRefToDictRefMap.get(variable);
                if (dictRef == null) {
                    return variable;
                }
                final Type returnType;
                if (variable.getType().matchesType(originalFn.getReturnType())) {
                    returnType = newFn.getReturnType();
                } else if (originalFn.getIntermediateType() != null &&
                        variable.getType().matchesType(originalFn.getIntermediateType())) {
                    returnType = newFn.getIntermediateType();
                } else {
                    returnType = getDictifiedType(variable.getType());
                }
                return new ColumnRefOperator(dictRef.getId(), returnType, dictRef.getName(), dictRef.isNullable());
            }
            return supportColumns.contains(variable) ?
                    stringRefToDictRefMap.getOrDefault(variable, variable) : variable;
        }

        AggregateFunction buildAggregateFunction(AggregateFunction fn, List<ScalarOperator> newChildren) {
            Preconditions.checkState(fn.getNumArgs() <= newChildren.size());
            final List<Type> argTypes =
                    newChildren.subList(0, fn.getNumArgs()).stream().map(ScalarOperator::getType).toList();
            record TypeInfo(Type intermediateType, Type returnType) {}
            TypeInfo typeInfo = switch (fn.functionName()) {
                case FunctionSet.ARRAY_AGG -> new TypeInfo(
                            new StructType(argTypes.stream().map(t -> (Type) new ArrayType(t)).toList()),
                            new ArrayType(argTypes.get(0)));
                case FunctionSet.ANY_VALUE -> new TypeInfo(argTypes.get(0), argTypes.get(0));
                default -> new TypeInfo(
                        getDictifiedType(fn.getIntermediateType()), getDictifiedType(fn.getReturnType()));
            };
            AggregateFunction newFn = (AggregateFunction) fn.copy();
            newFn.setArgsType(argTypes.toArray(Type[]::new));
            newFn.setIntermediateType(typeInfo.intermediateType);
            newFn.setRetType(typeInfo.returnType);
            return newFn;
        }

        @Override
        public ScalarOperator visitCall(CallOperator call, Void ignore) {
            boolean[] hasChange = new boolean[1];
            List<ScalarOperator> newChildren = visitList(call.getChildren(), hasChange);

            if (call.getFunction() instanceof AggregateFunction origFn) {
                if (this.newFn == null) {
                    originalFn = origFn;
                    newFn = buildAggregateFunction(origFn, newChildren);
                }
                Type returnType = call.getType().matchesType(origFn.getReturnType())
                        ? newFn.getReturnType() : newFn.getIntermediateType();
                CallOperator newCall = new CallOperator(call.getFnName(), returnType, newChildren, newFn,
                        call.isDistinct(), call.isRemovedDistinct());
                newCall.setIgnoreNulls(call.getIgnoreNulls());
                return newCall;
            }

            if (!hasChange[0]) {
                return call;
            }

            return buildCallOperator(call, newChildren);
        }
    }

    private class GlobalDictRewriter extends BaseScalarOperatorShuttle {
        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void ignore) {
            // string dict expression use origin string column
            ScalarOperator define = stringRefToDefineExprMap.get(variable.getId());
            if (define.isColumnRef() && variable.getId() == ((ColumnRefOperator) define).getId()) {
                // mock to string column
                return new ColumnRefOperator(variable.getId(), variable.getType(), variable.getName(),
                        variable.isNullable());
            }
            ScalarOperator result = define.accept(this, null);
            if (result.isColumnRef() && result.getType().isArrayType() && variable.getType().isStringType()) {
                // This is result of an unnest operator on an ARRAY<VARCHAR>, we replace the type with INT so that
                // backend doesn't get confused when creating dictionaries.
                ColumnRefOperator ref = result.cast();
                return new ColumnRefOperator(ref.getId(), IntegerType.INT, ref.getName(), ref.isNullable());
            }
            return result;
        }

        @Override
        public ScalarOperator visitCollectionElement(CollectionElementOperator collectionElementOp, Void context) {
            return collectionElementOp.getChild(0).accept(this, context);
        }

        @Override
        public ScalarOperator visitCall(CallOperator call, Void context) {
            if (call.getFunction() instanceof AggregateFunction) {
                return call.getChild(0).accept(this, context);
            }
            if (FunctionSet.ARRAY_MAP.equals(call.getFnName())) {
                return getLambdaFunctionArg(call).getLambdaExpr().accept(this, context);
            }
            if (isSupportedArrayFunction(call)) {
                return call.getChild(0).accept(this, context);
            }
            return super.visitCall(call, context);
        }

        @Override
        public ScalarOperator visitSubfield(SubfieldOperator operator, Void context) {
            Preconditions.checkState(operator.getFieldNames().size() == 1);
            Map<String, ColumnRefOperator> fieldData = structManager.getFieldStringRefMap(operator.getChild(0));
            Preconditions.checkNotNull(fieldData);
            ColumnRefOperator useStringRef = fieldData.get(operator.getFieldNames().get(0));
            Preconditions.checkNotNull(useStringRef);
            return useStringRef.accept(this, context);
        }
    }

    private boolean isSupportedSingleInputArrayFunction(CallOperator call) {
        // Array Function may has same name with String Function
        return LOW_CARD_ARRAY_FUNCTIONS.contains(call.getFnName()) &&
                Arrays.stream(call.getFunction().getArgs()).anyMatch(Type::isArrayType);
    }

    private boolean isSupportedArrayFunction(CallOperator call) {
        // Array Function may has same name with String Function
        return isSupportedSingleInputArrayFunction(call)
                || LOW_CARD_MULTI_INPUT_ARRAY_FUNCTIONS.contains(call.getFnName());
    }
}
