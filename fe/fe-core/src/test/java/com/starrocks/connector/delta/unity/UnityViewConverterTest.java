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

package com.starrocks.connector.delta.unity;

import com.databricks.sdk.service.catalog.ColumnInfo;
import com.databricks.sdk.service.catalog.ColumnTypeName;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TableType;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.DeltaLakeView;
import com.starrocks.catalog.PrimitiveType;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Type;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.sql.analyzer.AnalyzerUtils;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.TableRelation;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;
import java.util.function.UnaryOperator;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public class UnityViewConverterTest {
    private static ColumnInfo column(String name, String typeText, long position) {
        ColumnInfo ci = new ColumnInfo()
                .setName(name)
                .setTypeText(typeText)
                .setPosition(position);
        ColumnTypeName typeName = typeName(typeText);
        if (typeName != null) {
            ci.setTypeName(typeName);
        }
        return ci;
    }

    private static ColumnTypeName typeName(String typeText) {
        switch (typeText) {
            case "INT":
                return ColumnTypeName.INT;
            case "BIGINT":
                return ColumnTypeName.LONG;
            case "STRING":
                return ColumnTypeName.STRING;
            case "TIMESTAMP_NTZ":
                return ColumnTypeName.TIMESTAMP_NTZ;
            default:
                return null;
        }
    }

    private static ColumnInfo typedColumn(ColumnTypeName typeName) {
        return new ColumnInfo()
                .setName("c")
                .setTypeText(typeName == null ? "mystery" : typeName.name())
                .setTypeName(typeName)
                .setPosition(0L);
    }

    private static TableInfo singleColumnView(ColumnInfo column) {
        return new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT c FROM t"))
                .setColumns(ImmutableList.of(column));
    }

    private static TableInfo viewInfo(String sql) {
        return new TableInfo()
                .setFullName("main.sales.orders_view")
                .setCatalogName("main")
                .setSchemaName("sales")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, sql))
                .setColumns(ImmutableList.of(
                        column("id", "INT", 0L),
                        column("name", "STRING", 1L)));
    }

    private static TableName singleRelationName(DeltaLakeView view) {
        QueryStatement statement = view.getQueryStatement();
        List<TableRelation> tableRelations = AnalyzerUtils.collectTableRelations(statement);

        Assertions.assertEquals(1, tableRelations.size());
        return tableRelations.get(0).getName();
    }

    private static long countRelation(List<TableRelation> tableRelations, String catalog, String db, String table) {
        return tableRelations.stream()
                .map(TableRelation::getName)
                .filter(name -> java.util.Objects.equals(catalog, name.getCatalog()))
                .filter(name -> java.util.Objects.equals(db, name.getDb()))
                .filter(name -> java.util.Objects.equals(table, name.getTbl()))
                .count();
    }

    @Test
    public void testToDeltaLakeViewReadsSqlFromCelonisProperty() {
        TableInfo info = viewInfo("SELECT id, name FROM `default`.`foo`")
                .setViewDefinition("SELECT * FROM default.foo");

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertEquals("unity_demo", view.getCatalogName());
        Assertions.assertEquals("sales", view.getCatalogDBName());
        Assertions.assertEquals("orders_view", view.getCatalogTableName());
        Assertions.assertEquals("SELECT id, name FROM `default`.`foo`", view.getInlineViewDef());

        List<Column> columns = view.getBaseSchema();
        Assertions.assertEquals(2, columns.size());
        Assertions.assertEquals("id", columns.get(0).getName());
        Assertions.assertEquals(PrimitiveType.INT, columns.get(0).getType().getPrimitiveType());
        Assertions.assertEquals("name", columns.get(1).getName());
        Assertions.assertTrue(columns.get(1).getType().isStringType());
    }

    private static Stream<Arguments> noCompatibleViewDefinitionCases() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of("view_definition.starrocks", "SELECT id FROM t"))
                .setColumns(ImmutableList.of(column("id", "INT", 0L)));

        TableInfo missingProperties = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setColumns(ImmutableList.of(column("id", "INT", 0L)));

        return Stream.of(
                Arguments.of("property missing", info),
                Arguments.of("table info missing", null),
                Arguments.of("properties missing", missingProperties),
                Arguments.of("property blank", viewInfo("")));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("noCompatibleViewDefinitionCases")
    public void testToDeltaLakeViewThrowsWhenNoCompatibleViewDefinition(String name, TableInfo info) {
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> UnityViewConverter.toDeltaLakeView("unity_demo", "sales", "orders_view", info));
        Assertions.assertEquals("Unity Catalog view unity_demo.sales.orders_view has no compatible view definition",
                ex.getMessage());
    }

    @Test
    public void testToDeltaLakeViewThrowsWhenColumnsMissing() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT 1"))
                .setColumns(Collections.emptyList());

        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> UnityViewConverter.toDeltaLakeView("unity_demo", "sales", "orders_view", info));
    }

    @Test
    public void testToDeltaLakeViewThrowsWhenColumnsAreNull() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT 1"));

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> UnityViewConverter.toDeltaLakeView("unity_demo", "sales", "orders_view", info));
        Assertions.assertEquals("Unity Catalog view unity_demo.sales.orders_view has no column metadata; cannot build schema",
                ex.getMessage());
    }

    @Test
    public void testToDeltaLakeViewOrdersColumnsByPosition() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT id, total FROM t"))
                .setColumns(ImmutableList.of(
                        column("total", "BIGINT", 1L),
                        column("id", "INT", 0L)));

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertEquals(ImmutableList.of("id", "total"),
                view.getBaseSchema().stream().map(Column::getName).collect(Collectors.toList()));
    }

    @Test
    public void testToDeltaLakeViewThrowsForUnparseableTypeText() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT id, region FROM t"))
                .setColumns(ImmutableList.of(
                        column("id", "INT", 0L),
                        column("region", "geography", 1L)));

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> UnityViewConverter.toDeltaLakeView("unity_demo", "sales", "orders_view", info));
        Assertions.assertEquals(
                "Failed to convert UC column type 'geography' on view unity_demo.sales.orders_view",
                ex.getMessage());
    }

    @Test
    public void testToDeltaLakeViewPreservesUcCommentAsDisplayComment() {
        TableInfo info = viewInfo("SELECT id FROM t")
                .setComment("daily orders rollup");

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertEquals("daily orders rollup", view.getComment());
    }

    @Test
    public void testToDeltaLakeViewNullableDefaultsToTrue() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT id FROM t"))
                .setColumns(ImmutableList.of(
                        new ColumnInfo().setName("id").setTypeText("INT")
                                .setTypeName(ColumnTypeName.INT).setPosition(0L)));

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertTrue(view.getBaseSchema().get(0).isAllowNull());
    }

    @Test
    public void testToDeltaLakeViewConvertsTimestampNtz() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT created_at FROM t"))
                .setColumns(ImmutableList.of(column("created_at", "TIMESTAMP_NTZ", 0L)));

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertEquals(PrimitiveType.DATETIME, view.getBaseSchema().get(0).getType().getPrimitiveType());
    }

    @Test
    public void testToDeltaLakeViewResolvesScalarsFromTypeNameRegardlessOfTypeJson() {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT id, qty FROM t"))
                .setColumns(ImmutableList.of(
                        new ColumnInfo().setName("id").setTypeText("int")
                                .setTypeName(ColumnTypeName.INT).setTypeJson("\"int\"").setPosition(0L),
                        new ColumnInfo().setName("qty").setTypeText("int")
                                .setTypeName(ColumnTypeName.INT).setPosition(1L)));

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertEquals(PrimitiveType.INT, view.getBaseSchema().get(0).getType().getPrimitiveType());
        Assertions.assertEquals(PrimitiveType.INT, view.getBaseSchema().get(1).getType().getPrimitiveType());
    }

    @Test
    public void testToDeltaLakeViewConvertsNestedTypeFromTypeJson() {
        ColumnInfo tags = new ColumnInfo()
                .setName("tags")
                .setTypeText("array<int>")
                .setTypeName(ColumnTypeName.ARRAY)
                .setTypeJson("{\"type\":\"array\",\"elementType\":\"integer\",\"containsNull\":true}")
                .setPosition(0L);
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders_view")
                .setName("orders_view")
                .setTableType(TableType.VIEW)
                .setProperties(ImmutableMap.of(
                        UnityViewConverter.STARROCKS_VIEW_DEFINITION_PROPERTY, "SELECT tags FROM t"))
                .setColumns(ImmutableList.of(tags));

        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", info);

        Assertions.assertTrue(view.getBaseSchema().get(0).getType().isArrayType());
    }

    private static Stream<Arguments> supportedTypeCases() {
        return Stream.of(
                Arguments.of("boolean", typedColumn(ColumnTypeName.BOOLEAN),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.BOOLEAN, t.getPrimitiveType())),
                Arguments.of("byte", typedColumn(ColumnTypeName.BYTE),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.TINYINT, t.getPrimitiveType())),
                Arguments.of("short", typedColumn(ColumnTypeName.SHORT),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.SMALLINT, t.getPrimitiveType())),
                Arguments.of("int", typedColumn(ColumnTypeName.INT),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.INT, t.getPrimitiveType())),
                Arguments.of("long", typedColumn(ColumnTypeName.LONG),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.BIGINT, t.getPrimitiveType())),
                Arguments.of("float", typedColumn(ColumnTypeName.FLOAT),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.FLOAT, t.getPrimitiveType())),
                Arguments.of("double", typedColumn(ColumnTypeName.DOUBLE),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.DOUBLE, t.getPrimitiveType())),
                Arguments.of("date", typedColumn(ColumnTypeName.DATE),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.DATE, t.getPrimitiveType())),
                Arguments.of("timestamp", typedColumn(ColumnTypeName.TIMESTAMP),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.DATETIME, t.getPrimitiveType())),
                Arguments.of("timestamp_ntz", typedColumn(ColumnTypeName.TIMESTAMP_NTZ),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.DATETIME, t.getPrimitiveType())),
                Arguments.of("string", typedColumn(ColumnTypeName.STRING),
                        (Consumer<Type>) t -> Assertions.assertTrue(t.isStringType())),
                Arguments.of("char", typedColumn(ColumnTypeName.CHAR).setTypeText("char(10)"),
                        (Consumer<Type>) t -> {
                            Assertions.assertEquals(PrimitiveType.CHAR, t.getPrimitiveType());
                            Assertions.assertEquals(10, ((ScalarType) t).getLength());
                        }),
                Arguments.of("char without length", typedColumn(ColumnTypeName.CHAR).setTypeText("char"),
                        (Consumer<Type>) t -> Assertions.assertTrue(t.isStringType())),
                Arguments.of("binary", typedColumn(ColumnTypeName.BINARY),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.VARBINARY, t.getPrimitiveType())),
                Arguments.of("null", typedColumn(ColumnTypeName.NULL),
                        (Consumer<Type>) t -> Assertions.assertEquals(PrimitiveType.BOOLEAN, t.getPrimitiveType())),
                Arguments.of("decimal", typedColumn(ColumnTypeName.DECIMAL).setTypePrecision(18L).setTypeScale(4L),
                        (Consumer<Type>) t -> {
                            Assertions.assertTrue(t.isDecimalOfAnyVersion());
                            Assertions.assertEquals(18, ((ScalarType) t).getScalarPrecision());
                            Assertions.assertEquals(4, ((ScalarType) t).getScalarScale());
                        }),
                Arguments.of("array", typedColumn(ColumnTypeName.ARRAY)
                                .setTypeJson("{\"type\":\"array\",\"elementType\":\"integer\",\"containsNull\":true}"),
                        (Consumer<Type>) t -> Assertions.assertTrue(t.isArrayType())),
                Arguments.of("map", typedColumn(ColumnTypeName.MAP)
                                .setTypeJson("{\"type\":\"map\",\"keyType\":\"string\",\"valueType\":\"integer\","
                                        + "\"valueContainsNull\":true}"),
                        (Consumer<Type>) t -> Assertions.assertTrue(t.isMapType())),
                Arguments.of("struct", typedColumn(ColumnTypeName.STRUCT)
                                .setTypeJson("{\"type\":\"struct\",\"fields\":[{\"name\":\"a\",\"type\":\"integer\","
                                        + "\"nullable\":true,\"metadata\":{}}]}"),
                        (Consumer<Type>) t -> Assertions.assertTrue(t.isStructType())));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("supportedTypeCases")
    public void testToDeltaLakeViewConvertsSupportedUcType(String name, ColumnInfo column, Consumer<Type> assertion) {
        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", singleColumnView(column));

        assertion.accept(view.getBaseSchema().get(0).getType());
    }

    private static Stream<Arguments> unsupportedTypeCases() {
        return Stream.of(
                Arguments.of("interval", typedColumn(ColumnTypeName.INTERVAL)),
                Arguments.of("variant", typedColumn(ColumnTypeName.VARIANT)),
                Arguments.of("user_defined_type", typedColumn(ColumnTypeName.USER_DEFINED_TYPE)),
                Arguments.of("table_type", typedColumn(ColumnTypeName.TABLE_TYPE)),
                Arguments.of("geometry", typedColumn(ColumnTypeName.GEOMETRY)),
                Arguments.of("geography", typedColumn(ColumnTypeName.GEOGRAPHY)),
                Arguments.of("missing_type_name", typedColumn(null)));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("unsupportedTypeCases")
    public void testToDeltaLakeViewThrowsForUnsupportedUcType(String name, ColumnInfo column) {
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> UnityViewConverter.toDeltaLakeView("unity_demo", "sales", "orders_view", singleColumnView(column)));
    }

    private static Stream<Arguments> relationRewriteCases() {
        return Stream.of(
                Arguments.of("rewrite UC catalog",
                        "SELECT id FROM `main`.`sales`.`orders`",
                        (UnaryOperator<TableInfo>) info -> info,
                        "unity_demo", "sales", "orders"),
                Arguments.of("rewrite UC catalog from full name",
                        "SELECT id FROM `main`.`sales`.`orders`",
                        (UnaryOperator<TableInfo>) info -> info.setCatalogName(null),
                        "unity_demo", "sales", "orders"),
                Arguments.of("default unqualified relation",
                        "SELECT id FROM orders",
                        (UnaryOperator<TableInfo>) info -> info,
                        "unity_demo", "sales", "orders"),
                Arguments.of("keep other catalog",
                        "SELECT id FROM `other_catalog`.`sales`.`orders`",
                        (UnaryOperator<TableInfo>) info -> info,
                        "other_catalog", "sales", "orders"),
                Arguments.of("keep explicit catalog without source catalog",
                        "SELECT id FROM `main`.`sales`.`orders`",
                        (UnaryOperator<TableInfo>) info -> info.setCatalogName(null).setFullName("orders_view"),
                        "main", "sales", "orders"),
                Arguments.of("skip CTE relation",
                        "WITH orders AS (SELECT 1 AS id) SELECT id FROM orders",
                        (UnaryOperator<TableInfo>) info -> info,
                        null, null, "orders"));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("relationRewriteCases")
    public void testToDeltaLakeViewFormatsRelations(String name, String sql,
                                                    UnaryOperator<TableInfo> tableInfoCustomizer,
                                                    String expectedCatalog, String expectedDb, String expectedTable) {
        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", tableInfoCustomizer.apply(viewInfo(sql)));

        TableName tableName = singleRelationName(view);
        Assertions.assertEquals(expectedCatalog, tableName.getCatalog());
        Assertions.assertEquals(expectedDb, tableName.getDb());
        Assertions.assertEquals(expectedTable, tableName.getTbl());
    }

    @Test
    public void testToDeltaLakeViewSkipsNestedCteReferences() {
        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", viewInfo(
                        "WITH outer_cte AS (" +
                                "WITH scoped AS (SELECT id FROM orders) SELECT id FROM scoped" +
                                ") SELECT id FROM outer_cte"));

        List<TableRelation> tableRelations = AnalyzerUtils.collectTableRelations(view.getQueryStatement());
        Assertions.assertEquals(3, tableRelations.size());
        Assertions.assertEquals(1L, countRelation(tableRelations, "unity_demo", "sales", "orders"));
        Assertions.assertEquals(1L, countRelation(tableRelations, null, null, "scoped"));
        Assertions.assertEquals(1L, countRelation(tableRelations, null, null, "outer_cte"));
    }

    @Test
    public void testToDeltaLakeViewFormatsPhysicalTableThatMatchesNestedCteName() {
        DeltaLakeView view = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", viewInfo(
                        "WITH outer_cte AS (" +
                                "WITH orders AS (SELECT 1 AS id) SELECT id FROM orders" +
                                ") SELECT id FROM orders"));

        List<TableRelation> tableRelations = AnalyzerUtils.collectTableRelations(view.getQueryStatement());
        Assertions.assertEquals(2, tableRelations.size());
        Assertions.assertEquals(1L, countRelation(tableRelations, "unity_demo", "sales", "orders"));
        Assertions.assertEquals(1L, countRelation(tableRelations, null, null, "orders"));
    }

    @Test
    public void testDeltaLakeViewEqualityUsesCatalogDatabaseAndViewName() {
        DeltaLakeView first = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", viewInfo("SELECT id FROM orders"));
        DeltaLakeView second = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "orders_view", viewInfo("SELECT id FROM orders"));
        DeltaLakeView otherName = UnityViewConverter.toDeltaLakeView(
                "unity_demo", "sales", "other_view", viewInfo("SELECT id FROM orders"));

        Assertions.assertEquals(first, second);
        Assertions.assertEquals(first.hashCode(), second.hashCode());
        Assertions.assertNotEquals(first, otherName);
    }
}
