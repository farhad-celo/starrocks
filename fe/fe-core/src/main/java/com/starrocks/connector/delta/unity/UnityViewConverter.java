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
import com.google.common.base.Strings;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.DeltaLakeView;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Type;
import com.starrocks.connector.ColumnTypeConverter;
import com.starrocks.connector.ConnectorTableId;
import com.starrocks.connector.exception.StarRocksConnectorException;
import io.delta.kernel.internal.types.DataTypeJsonSerDe;
import io.delta.kernel.types.StructType;

import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

import static io.delta.kernel.internal.util.ColumnMapping.ColumnMappingMode.NONE;

final class UnityViewConverter {
    static final String STARROCKS_VIEW_DEFINITION_PROPERTY = "celonis.sql.starrocks";

    private UnityViewConverter() {
    }

    static DeltaLakeView toDeltaLakeView(String catalogName, String dbName, String tableName, TableInfo info) {
        Map<String, String> properties = info == null || info.getProperties() == null
                ? Collections.emptyMap()
                : info.getProperties();
        String sql = properties.get(STARROCKS_VIEW_DEFINITION_PROPERTY);
        if (Strings.isNullOrEmpty(sql)) {
            throw new StarRocksConnectorException(
                    "Unity Catalog view %s.%s.%s has no compatible view definition",
                    catalogName, dbName, tableName);
        }

        Collection<ColumnInfo> ucColumns = info.getColumns();
        if (ucColumns == null || ucColumns.isEmpty()) {
            throw new StarRocksConnectorException(
                    "Unity Catalog view %s.%s.%s has no column metadata; cannot build schema",
                    catalogName, dbName, tableName);
        }
        List<Column> columns = ucColumns.stream()
                .sorted(Comparator.comparingLong(c -> c.getPosition() == null ? 0L : c.getPosition()))
                .map(ci -> toSrColumn(catalogName, dbName, tableName, ci))
                .collect(Collectors.toList());

        DeltaLakeView view = new DeltaLakeView(
                ConnectorTableId.CONNECTOR_ID_GENERATOR.getNextId().asInt(),
                catalogName,
                dbName,
                tableName,
                columns,
                sql,
                new DeltaLakeView.RelationRewriteContext(catalogName, dbName, sourceCatalogName(info)));
        if (!Strings.isNullOrEmpty(info.getComment())) {
            view.setComment(info.getComment());
        }
        return view;
    }

    private static String sourceCatalogName(TableInfo info) {
        if (!Strings.isNullOrEmpty(info.getCatalogName())) {
            return info.getCatalogName();
        }
        String fullName = info.getFullName();
        if (Strings.isNullOrEmpty(fullName)) {
            return null;
        }
        int dot = fullName.indexOf('.');
        return dot > 0 ? fullName.substring(0, dot) : null;
    }

    private static Column toSrColumn(String catalogName, String dbName, String tableName, ColumnInfo ci) {
        Type type = fromUnityColumnType(ci);
        if (type == null || type.isUnknown()) {
            throw new StarRocksConnectorException(
                    "Failed to convert UC column type '" + ci.getTypeText() + "' on view " +
                            catalogName + "." + dbName + "." + tableName);
        }
        boolean nullable = ci.getNullable() == null || ci.getNullable();
        String comment = ci.getComment() == null ? "" : ci.getComment();
        return new Column(ci.getName(), type, nullable, comment);
    }

    private static Type fromUnityColumnType(ColumnInfo ci) {
        ColumnTypeName typeName = ci.getTypeName();
        if (typeName == null) {
            return Type.UNKNOWN_TYPE;
        }
        switch (typeName) {
            case BOOLEAN:
                return Type.BOOLEAN;
            case BYTE:
                return Type.TINYINT;
            case SHORT:
                return Type.SMALLINT;
            case INT:
                return Type.INT;
            case LONG:
                return Type.BIGINT;
            case FLOAT:
                return Type.FLOAT;
            case DOUBLE:
                return Type.DOUBLE;
            case DATE:
                return Type.DATE;
            case TIMESTAMP:
            case TIMESTAMP_NTZ:
                return Type.DATETIME;
            case STRING:
                return ScalarType.createDefaultCatalogString();
            case CHAR:
                return fromCharType(ci.getTypeText());
            case BINARY:
                return Type.VARBINARY;
            case NULL:
                // Void columns carry only nulls; StarRocks resolves the null type to boolean.
                return Type.BOOLEAN;
            case DECIMAL:
                int precision = ci.getTypePrecision() == null ? 10 : ci.getTypePrecision().intValue();
                int scale = ci.getTypeScale() == null ? 0 : ci.getTypeScale().intValue();
                return ScalarType.createUnifiedDecimalType(precision, scale);
            case ARRAY:
            case STRUCT:
            case MAP:
                return fromNestedTypeJson(ci.getTypeJson());
            default:
                return Type.UNKNOWN_TYPE;
        }
    }

    private static Type fromCharType(String typeText) {
        if (!Strings.isNullOrEmpty(typeText)) {
            try {
                int length = ColumnTypeConverter.getCharLength(typeText);
                if (length >= 0 && length <= ScalarType.MAX_CHAR_LENGTH) {
                    return ScalarType.createCharType(length);
                }
            } catch (StarRocksConnectorException ignored) {
                // No explicit char length; fall back to the default string type.
            }
        }
        return ScalarType.createDefaultCatalogString();
    }

    private static Type fromNestedTypeJson(String typeJson) {
        if (Strings.isNullOrEmpty(typeJson)) {
            return Type.UNKNOWN_TYPE;
        }
        try {
            // Delta Kernel only exposes public JSON deserialization for StructType, so wrap the
            // complex type as a single field and reuse the standard Delta type converter.
            String fieldJson = "{\"type\":\"struct\",\"fields\":[{\"name\":\"column\",\"type\":" +
                    typeJson + ",\"nullable\":true,\"metadata\":{}}]}";
            StructType schema = DataTypeJsonSerDe.deserializeStructType(fieldJson);
            return ColumnTypeConverter.fromDeltaLakeType(schema.at(0).getDataType(), NONE.value);
        } catch (InternalError | Exception e) {
            return Type.UNKNOWN_TYPE;
        }
    }
}
