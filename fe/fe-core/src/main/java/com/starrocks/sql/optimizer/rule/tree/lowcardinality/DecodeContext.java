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
import com.starrocks.catalog.FunctionSet;
import com.starrocks.sql.ast.expression.ExprUtils;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.base.ColumnRefSet;
import com.starrocks.sql.optimizer.operator.Operator;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.CollectionElementOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.DictMappingOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.operator.scalar.SubfieldOperator;
import com.starrocks.sql.optimizer.rewrite.BaseScalarOperatorShuttle;
import com.starrocks.sql.optimizer.statistics.ColumnDict;
import com.starrocks.type.ArrayType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.StructField;
import com.starrocks.type.StructType;
import com.starrocks.type.Type;

import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeCollector.LOW_CARD_ARRAY_FUNCTIONS;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeCollector.LOW_CARD_STRUCT_FUNCTIONS;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeUtil.ExprReplacer;
import static com.starrocks.sql.optimizer.rule.tree.lowcardinality.DecodeUtil.getDictifiedType;

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
    Map<Integer, List<ScalarOperator>> stringExprsMap = Maps.newHashMap();

    // all string aggregate expressions
    List<CallOperator> stringAggregateExprs = Lists.newArrayList();

    // The string columns used by the operator
    // IdentityHashMap: use object == object, operator equals is not enough
    Map<Operator, DecodeInfo> operatorDecodeInfo = Maps.newIdentityHashMap();

    // global dict expressions
    Map<Integer, ScalarOperator> globalDictsExpr = Maps.newHashMap();

    Map<ColumnRefOperator, ColumnRefOperator> stringRefToDictRefMap = Maps.newHashMap();

    Map<ColumnRefOperator, ScalarOperator> dictRefToDefineExprMap = Maps.newHashMap();

    Map<ScalarOperator, ScalarOperator> stringExprToDictExprMap = Maps.newHashMap();

    // Maintains a mapping from struct ColumnRefs to field ColumnRefs to use for encoded fields.
    // Currently only fields which correspond to a ColumnRef can be encoded. All field ColumnRefOperators should have
    // STRING or ARRAY<STRING> types. Nested structs are not supported.
    Map<Integer, Map<String, ColumnRefOperator>> structRefToFieldUseStringRefMap = Maps.newHashMap();

    // Maintains a mapping from struct operators to field ColumnRefs to use for encoded fields.
    // Currently only fields which correspond to a ColumnRef can be encoded. All field ColumnRefOperators should have
    // STRING or ARRAY<STRING> types. Nested structs are not supported.
    Map<ScalarOperator, Map<String, ColumnRefOperator>> structOpToFieldUseStringRefMap =
            Maps.newIdentityHashMap();

    UnionDictionaryManager unionDictionaryManager;

    DecodeContext(ColumnRefFactory factory) {
        this.factory = factory;
    }

    public void initRewriteExpressions() {
        rewriteStringRefToDictRef();
        rewriteGlobalDict();
        rewriteStringExpressions();
        rewriteStringAggregations();
    }

    Map<String, ColumnRefOperator> getFieldUseStringRefMap(ScalarOperator operator) {
        if (operator.isColumnRef()) {
            ColumnRefOperator c = operator.cast();
            return structRefToFieldUseStringRefMap.get(c.getId());
        }
        return structOpToFieldUseStringRefMap.get(operator);

    }

    ColumnRefOperator getUseStringRef(ScalarOperator operator) {
        if (operator.isColumnRef()) {
            return (ColumnRefOperator) operator;
        }
        if (operator instanceof SubfieldOperator) {
            SubfieldOperator subfieldOperator = operator.cast();
            Map<String, ColumnRefOperator> fieldsUseRefMap = getFieldUseStringRefMap(subfieldOperator.getChild(0));
            Preconditions.checkNotNull(fieldsUseRefMap);
            Preconditions.checkState(subfieldOperator.getFieldNames().size() == 1
                    && fieldsUseRefMap.containsKey(subfieldOperator.getFieldNames().get(0)));
            return getUseStringRef(fieldsUseRefMap.get(subfieldOperator.getFieldNames().get(0)));
        }
        if (operator instanceof CallOperator && FunctionSet.ARRAY_AGG.equals(((CallOperator) operator).getFnName())) {
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
        DictExprRewrite exprRewriter = new DictExprRewrite();
        for (Integer stringId : allStringColumns) {
            if (!stringRefToDefineExprMap.containsKey(stringId)) {
                continue;
            }
            ColumnRefOperator stringRef = factory.getColumnRef(stringId);
            ColumnRefOperator dictRef = createNewDictColumn(stringRef);
            stringRefToDictRefMap.put(stringRef, dictRef);
            stringExprToDictExprMap.put(stringRef, exprRewriter.decode(dictRef, stringRef, stringRef));
        }

        // rewrite string column define expression
        for (Integer stringId : stringRefToDefineExprMap.keySet()) {
            ScalarOperator stringDefineExpr = stringRefToDefineExprMap.get(stringId);
            ColumnRefOperator stringRef = factory.getColumnRef(stringId);
            ColumnRefOperator dictRef = stringRefToDictRefMap.get(stringRef);
            ColumnRefOperator useStringRef = stringRef.getType().isStructType() ? stringRef
                    : getUseStringRef(stringDefineExpr);
            // return type is dict
            ScalarOperator dictExpr = exprRewriter.define(dictRef.getType(), useStringRef, stringDefineExpr);
            dictRefToDefineExprMap.put(dictRef, dictExpr);
        }
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

            ScalarOperator defineExpr = stringDefineExpr.accept(dictRewriter, null);
            List<ColumnRefOperator> defineUsedStringRef = defineExpr.getColumnRefs();
            Preconditions.checkState(!defineUsedStringRef.isEmpty());

            ColumnRefOperator defineUsedDictRef = stringRefToDictRefMap.get(defineUsedStringRef.get(0));
            ScalarOperator globalDictExpr = new DictMappingOperator(defineUsedDictRef, defineExpr, dictRef.getType());
            globalDictsExpr.put(dictRef.getId(), globalDictExpr);
        }
    }

    private void rewriteStringExpressions() {
        // rewrite string expression
        DictExprRewrite exprRewriter = new DictExprRewrite();
        for (Integer stringId : stringExprsMap.keySet()) {
            ColumnRefOperator stringRef = factory.getColumnRef(stringId);
            ColumnRefOperator dictRef = stringRefToDictRefMap.get(stringRef);
            for (ScalarOperator stringExpr : stringExprsMap.getOrDefault(stringId, Collections.emptyList())) {
                if (stringExprToDictExprMap.containsKey(stringExpr)) {
                    continue;
                }
                // return type is string, different as define expression
                ScalarOperator dictExpr = exprRewriter.decode(dictRef, stringRef, stringExpr);
                stringExprToDictExprMap.put(stringExpr, dictExpr);
            }
        }
    }

    private void rewriteStringAggregations() {
        // rewrite string aggregate expression
        AggregateRewriter rewriter = new AggregateRewriter();
        for (CallOperator aggFn : stringAggregateExprs) {
            CallOperator new1stAggFn = (CallOperator) (aggFn.accept(rewriter, null));
            stringExprToDictExprMap.put(aggFn, new1stAggFn);
        }
    }

    // create a new dictionary column and assign the same property except for the type and column id
    // the input column maybe a dictionary column or a string column
    private ColumnRefOperator createNewDictColumn(ColumnRefOperator column) {
        boolean isLambdaArg = column.getOpType().equals(OperatorType.LAMBDA_ARGUMENT);
        if (column.getType().isStringArrayType()) {
            return factory.create(column.getName(), ArrayType.ARRAY_INT, column.isNullable(), isLambdaArg);
        } else if (column.getType().isStringType()) {
            return factory.create(column.getName(), IntegerType.INT, column.isNullable(), isLambdaArg);
        } else if (column.getType().isStructType()) {
            Map<String, ColumnRefOperator> fieldsData = getFieldUseStringRefMap(column);
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
            return factory.create(column.getName(), dictType, column.isNullable(), isLambdaArg);
        } else {
            throw new IllegalArgumentException("Unsupported dictified type: " +  column.getType());
        }
    }

    private static Function buildFunction(String fnName, List<ScalarOperator> args) {
        if (fnName.equals(FunctionSet.NAMED_STRUCT)) {
            Type[] argTypes = args.stream().map(ScalarOperator::getType).toArray(Type[]::new);
            Function fn = ExprUtils.getBuiltinFunction(fnName, argTypes, Function.CompareMode.IS_SUPERTYPE_OF).copy();
            List<StructField> fields = Lists.newArrayList();
            for (int i = 0; i < args.size(); i += 2) {
                fields.add(new StructField(((ConstantOperator) args.get(i)).getVarchar(), argTypes[i + 1]));
            }
            fn.setRetType(new StructType(fields, true));
            return fn;
        }
        Type[] argTypes = args.stream().map(ScalarOperator::getType).toArray(Type[]::new);
        return ExprUtils.getBuiltinFunction(fnName, argTypes, Function.CompareMode.IS_SUPERTYPE_OF);
    }

    // define mode: means the result column is dict, DictExpr should return int/array<int> type
    // decode mode: means the result column is string, DictExpr should return string/array<string> type
    private class DictExprRewrite extends BaseScalarOperatorShuttle {
        // to mark special array expression: array_min/array_max/array[x]
        // their return type is string, but use low cardinality optimization, we need execute them first
        private ScalarOperator anchorOp;
        private ColumnRefOperator anchorUseDictRef;
        private ScalarOperator newAnchorOp;

        public ScalarOperator decodeStruct(ScalarOperator dictExpression,
                                           ScalarOperator expression,
                                           ColumnRefOperator useStringRef) {
            Preconditions.checkState(dictExpression.getType().isStructType());
            Preconditions.checkState(useStringRef.getType().isStructType());
            StructType exprType =  (StructType) expression.getType();
            StructType dictType = (StructType) dictExpression.getType();
            List<ScalarOperator> newFields = Lists.newArrayList();
            Map<String, ColumnRefOperator> fieldsStringRefMap = getFieldUseStringRefMap(useStringRef);
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

        void reset() {
            anchorOp = null;
            anchorUseDictRef = null;
            newAnchorOp = null;
        }

        void setDictColumn(ColumnRefOperator dictColumn, Type originalType) {
            Preconditions.checkState(!dictColumn.getType().isStructType()
                    && !dictColumn.getOpType().equals(OperatorType.LAMBDA_ARGUMENT));
            anchorUseDictRef = dictColumn;
            newAnchorOp = new ColumnRefOperator(dictColumn.getId(), originalType.isArrayType() ?
                    ((ArrayType) originalType).getItemType() : originalType,
                    dictColumn.getName(),
                    dictColumn.isNullable());
        }

        public ScalarOperator decode(ColumnRefOperator useDictRef, ColumnRefOperator useStringRef,
                                     ScalarOperator expression) {
            reset();
            anchorUseDictRef = useDictRef;
            ScalarOperator result = expression.accept(this, null);
            if (useStringRef.getType().isVarchar() && anchorUseDictRef.isColumnRef()) {
                return new DictMappingOperator(useDictRef, expression.clone(), expression.getType());
            }
            if (result.getType().isStructType()) {
                Preconditions.checkState(anchorOp == null);
                return decodeStruct(result, expression, useStringRef);
            }
            if (result.isColumnRef() && anchorOp == null && false) {
                // decode array-column-ref
                return new DictMappingOperator(useDictRef, result, expression.getType());
            } else if (result instanceof CallOperator &&
                    (FunctionSet.ARRAY_LENGTH.equalsIgnoreCase(((CallOperator) result).getFnName()) ||
                            FunctionSet.CARDINALITY.equalsIgnoreCase(((CallOperator) result).getFnName()))) {
                Preconditions.checkState(anchorOp == null);
                return result;
            } else if (result instanceof SubfieldOperator) {
                Preconditions.checkState(expression instanceof SubfieldOperator);
                SubfieldOperator subfieldOperator = expression.cast();
                if (subfieldOperator.getFieldNames().size() != 1 ||
                        !getFieldUseStringRefMap(expression.getChild(0))
                                .containsKey(subfieldOperator.getFieldNames().get(0))) {
                    // Getting non dictified field from a struct
                    return result;
                }
            }
            //result = processAnchor(result);
            return new DictMappingOperator(expression.getType(), anchorUseDictRef, newAnchorOp, result);
        }

        public ScalarOperator define(Type type, ColumnRefOperator useStringRef, ScalarOperator expression) {
            reset();
            ColumnRefOperator useDictRef = stringRefToDictRefMap.get(useStringRef);
            ScalarOperator result = expression.accept(this, null);
            if (useStringRef.getType().isVarchar() && anchorUseDictRef.isColumnRef()) {
                return new DictMappingOperator(useDictRef, expression.clone(), useDictRef.getType());

            }
            if (anchorOp != null) {
                if (!result.isColumnRef()) {
                    // e.g. upper(array_column[0])), need define string-expr by dict-expr
                    return new DictMappingOperator(type, anchorUseDictRef, result, anchorOp);
                } else {
                    // e.g. array_column[0], need define to string
                    return anchorOp;
                }
            }

            return result;
        }

        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void context) {
            ColumnRefOperator result = stringRefToDictRefMap.getOrDefault(variable, variable);
            if (!result.getType().isStructType() && !variable.getOpType().equals(OperatorType.LAMBDA_ARGUMENT)) {
                setDictColumn(result, variable.getType());
            }
            return result;
        }

        // DO NOT SUBMIT
        static ColumnRefOperator getColumnRef(ScalarOperator op) {
            if (op.isColumnRef()) {
                return op.cast();
            }
            List<ColumnRefOperator> columns = op.getChildren().stream().map(DictExprRewrite::getColumnRef)
                    .filter(Objects::nonNull).toList();
            Preconditions.checkState(columns.size() <= 1);
            return columns.stream().findFirst().orElse(null);
        }

        @Override
        public ScalarOperator visitCall(CallOperator call, Void context) {
            if (!isSupportedArrayFunction(call) && !LOW_CARD_STRUCT_FUNCTIONS.contains(call.getFnName())) {
                return super.visitCall(call, context);
            }
            if (FunctionSet.ARRAY_MAP.equalsIgnoreCase(call.getFnName())) {
                Preconditions.checkState(call.getChildren().size() == 2);
                ScalarOperator arrayParam = call.getChildren().get(1).accept(this, context);
                LambdaFunctionOperator lambdaFn = call.getChild(0).cast();
                ScalarOperator newLambdaFn = lambdaFn.getLambdaExpr().accept(this, null);
                ColumnRefOperator lambdaUsedDictRef = getColumnRef(newLambdaFn);
                ExprReplacer exprReplacer = new ExprReplacer(Map.of(lambdaUsedDictRef, newAnchorOp),
                        new ColumnRefSet(Collections.singleton(lambdaUsedDictRef)), DecodeContext.this);
                newAnchorOp = newLambdaFn.accept(exprReplacer, null);
                return arrayParam;
            }
            boolean[] hasChange = new boolean[1];
            List<ScalarOperator> newChildren = visitList(call.getChildren(), hasChange);
            if (!hasChange[0]) {
                return call;
            }

            Function fn = buildFunction(call.getFnName(), newChildren);
            ScalarOperator result = new CallOperator(call.getFnName(), fn.getReturnType(), newChildren, fn);

            if (FunctionSet.ARRAY_MAX.equalsIgnoreCase(call.getFnName()) ||
                    FunctionSet.ARRAY_MIN.equalsIgnoreCase(call.getFnName())) {
                return processAnchor(result);
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

            return processAnchor(result);
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
                Map<String, ColumnRefOperator> useFieldRefMap = getFieldUseStringRefMap(operator.getChild(0));
                Preconditions.checkNotNull(useFieldRefMap);
                ColumnRefOperator fieldUseStringRef = useFieldRefMap.get(operator.getFieldNames().get(0));
                Preconditions.checkNotNull(fieldUseStringRef);
                ColumnRefOperator childDictRef = stringRefToDictRefMap.get(fieldUseStringRef);
                Preconditions.checkNotNull(childDictRef);
                setDictColumn(childDictRef, fieldUseStringRef.getType());
                if (operator.getType().isStringType()) {
                    return processAnchor(result);
                } else {
                    return result;
                }
            }
            return result;
        }

        private ScalarOperator processAnchor(ScalarOperator expr) {
            if (anchorOp != null && !anchorOp.equals(expr)) {
                return expr;
            }
            // DO NOT SUBMIT
            return anchorOp = newAnchorOp;
        }
    }

    private class AggregateRewriter extends BaseScalarOperatorShuttle {
        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void ignore) {
            return stringRefToDictRefMap.getOrDefault(variable, variable);
        }

        AggregateFunction buildAggregateFunction(AggregateFunction fn, List<ScalarOperator> newChildren,
                                                 List<ScalarOperator> originalChildren) {
            final List<Type> argTypes;
            final Type intermediateType;
            final Type returnType;
            if (FunctionSet.ARRAY_AGG.equals(fn.functionName())) {
                ScalarOperator child = originalChildren.get(0);
                if (child.getType().matchesType(fn.getReturnType())
                        || child.getType().matchesType(fn.getIntermediateTypeOrReturnType())) {
                    argTypes = Lists.newArrayList();
                    Map<String, ColumnRefOperator> fieldMapping = getFieldUseStringRefMap(originalChildren.get(0));
                    Preconditions.checkNotNull(fieldMapping);
                    for (int i = 0; i < fn.getNumArgs(); ++i) {
                        argTypes.add(fieldMapping.containsKey("col" + (i + 1)) ? getDictifiedType(fn.getArgs()[i])
                                : fn.getArgs()[i]);
                    }
                } else {
                    argTypes = newChildren.stream().map(ScalarOperator::getType).toList();
                }
                intermediateType = new StructType(argTypes.stream().map(t -> (Type) new ArrayType(t)).toList());
                returnType = new ArrayType(argTypes.get(0));
            } else if (FunctionSet.ANY_VALUE.equals(fn.functionName())) {
                returnType = intermediateType = newChildren.get(0).getType();
                argTypes = List.of(newChildren.get(0).getType());
            } else {
                argTypes = Lists.newArrayListWithCapacity(fn.getNumArgs());
                for (int i = 0; i < fn.getNumArgs(); ++i) {
                    argTypes.add(getDictifiedType(fn.getArgs()[i]));
                }
                returnType = getDictifiedType(fn.getReturnType());
                intermediateType = getDictifiedType(fn.getIntermediateType());
            }
            AggregateFunction newFn = (AggregateFunction) fn.copy();
            Preconditions.checkState(argTypes.size() == fn.getNumArgs(),
                    "argTypes size %s doesn't match numArgs %s for function %s",
                    argTypes.size(), fn.getNumArgs(), fn.functionName());
            newFn.setArgsType(argTypes.toArray(Type[]::new));
            newFn.setIntermediateType(intermediateType);
            newFn.setRetType(returnType);
            return newFn;
        }

        @Override
        public ScalarOperator visitCall(CallOperator call, Void ignore) {
            boolean[] hasChange = new boolean[1];
            List<ScalarOperator> newChildren = visitList(call.getChildren(), hasChange);

            if (call.getFunction() instanceof AggregateFunction origFn) {
                AggregateFunction fn = buildAggregateFunction(origFn, newChildren, call.getArguments());
                ColumnRefOperator firstChild = newChildren.get(0).isColumnRef() ? newChildren.get(0).cast() : null;
                if (firstChild != null && firstChild != call.getArguments().get(0)) {
                    if (firstChild.getType().matchesType(fn.getReturnType())) {
                        newChildren.set(0, new ColumnRefOperator(firstChild.getId(), fn.getReturnType(),
                                firstChild.getName(), firstChild.isNullable()));
                    }
                    if (fn.getIntermediateType() != null
                            && firstChild.getType().matchesType(fn.getIntermediateType())) {
                        newChildren.set(0, new ColumnRefOperator(firstChild.getId(), fn.getIntermediateType(),
                                firstChild.getName(), firstChild.isNullable()));
                    }
                }
                Type returnType = call.getType().matchesType(origFn.getReturnType())
                        ? fn.getReturnType() : fn.getIntermediateType();
                CallOperator newCall = new CallOperator(call.getFnName(), returnType, newChildren, fn,
                        call.isDistinct(), call.isRemovedDistinct());
                newCall.setIgnoreNulls(call.getIgnoreNulls());
                return newCall;
            }

            if (!hasChange[0]) {
                return call;
            }

            Function fn = buildFunction(call.getFnName(), newChildren);
            return new CallOperator(call.getFnName(), fn.getReturnType(), newChildren, fn,
                    call.isDistinct(), call.isRemovedDistinct());
        }
    }

    private class GlobalDictRewriter extends BaseScalarOperatorShuttle {
        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void ignore) {
            // string dict expression use origin string column
            ScalarOperator res = stringRefToDefineExprMap.get(variable.getId());
            if (res.isColumnRef() && variable.getId() == ((ColumnRefOperator) res).getId()) {
                // mock to string column
                return new ColumnRefOperator(variable.getId(), variable.getType(), variable.getName(),
                        variable.isNullable());
            }
            return res.accept(this, null);
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
            if (isSupportedArrayFunction(call)) {
                return call.getChild(0).accept(this, context);
            }
            return super.visitCall(call, context);
        }

        @Override
        public ScalarOperator visitSubfield(SubfieldOperator operator, Void context) {
            Preconditions.checkState(operator.getFieldNames().size() == 1);
            Map<String, ColumnRefOperator> fieldData = getFieldUseStringRefMap(operator.getChild(0));
            Preconditions.checkNotNull(fieldData);
            ColumnRefOperator useStringRef = fieldData.get(operator.getFieldNames().get(0));
            Preconditions.checkNotNull(useStringRef);
            return useStringRef.accept(this, context);
        }
    }

    private boolean isSupportedArrayFunction(CallOperator call) {
        // Array Function may has same name with String Function
        return LOW_CARD_ARRAY_FUNCTIONS.contains(call.getFnName()) &&
                Arrays.stream(call.getFunction().getArgs()).anyMatch(Type::isArrayType);
    }
}
