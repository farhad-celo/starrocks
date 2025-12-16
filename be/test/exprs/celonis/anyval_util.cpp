#include "exprs/celonis/anyval_util.h"

namespace starrocks {

FunctionContext::TypeDesc CelonisAnyValUtil::column_type_to_type_desc(const TypeDescriptor& type) {
    FunctionContext::TypeDesc out;
    switch (type.type) {
    case TYPE_BOOLEAN:
        out.type = TYPE_BOOLEAN;
        break;
    case TYPE_TINYINT:
        out.type = TYPE_TINYINT;
        break;
    case TYPE_SMALLINT:
        out.type = TYPE_SMALLINT;
        break;
    case TYPE_INT:
        out.type = TYPE_INT;
        break;
    case TYPE_BIGINT:
        out.type = TYPE_BIGINT;
        break;
    case TYPE_LARGEINT:
        out.type = TYPE_LARGEINT;
        break;
    case TYPE_FLOAT:
        out.type = TYPE_FLOAT;
        break;
    case TYPE_TIME:
    case TYPE_DOUBLE:
        out.type = TYPE_DOUBLE;
        break;
    case TYPE_DATE:
        out.type = TYPE_DATE;
        break;
    case TYPE_DATETIME:
        out.type = TYPE_DATETIME;
        break;
    case TYPE_VARCHAR:
        out.type = TYPE_VARCHAR;
        out.len = type.len;
        break;
    case TYPE_PERCENTILE:
        out.type = TYPE_PERCENTILE;
        break;
    case TYPE_HLL:
        out.type = TYPE_HLL;
        out.len = type.len;
        break;
    case TYPE_OBJECT:
        out.type = TYPE_OBJECT;
        break;
    case TYPE_CHAR:
        out.type = TYPE_CHAR;
        out.len = type.len;
        break;
    case TYPE_DECIMAL:
        out.type = TYPE_DECIMAL;
        // out.precision = type.precision;
        // out.scale = type.scale;
        break;
    case TYPE_DECIMALV2:
        out.type = TYPE_DECIMALV2;
        // out.precision = type.precision;
        // out.scale = type.scale;
        break;
    case TYPE_NULL:
        out.type = TYPE_NULL;
        break;
    case TYPE_ARRAY:
    case TYPE_MAP: {
        out.type = type.type;
        for (const auto& child : type.children) {
            if (child.is_unknown_type()) {
                // TODO(celonis):
                // For Map type, if map's key or value is pruned, that column's type will be set to unknown for
                // partial materialize.
                // We should not use TYPE_UNKNOWN in the future, use another type, it may misleading other people.
                FunctionContext::TypeDesc child_out;
                child_out.type = TYPE_UNKNOWN;
                out.children.emplace_back(child_out);
            } else {
                out.children.emplace_back(column_type_to_type_desc(child));
            }
        }
        break;
    }
    case TYPE_STRUCT: {
        out.type = type.type;
        for (const auto& name : type.field_names) {
            out.field_names.emplace_back(name);
        }
        for (const auto& child : type.children) {
            out.children.emplace_back(column_type_to_type_desc(child));
        }
        break;
    }
    case TYPE_DECIMAL32:
        out.type = TYPE_DECIMAL32;
        out.precision = type.precision;
        out.scale = type.scale;
        break;
    case TYPE_DECIMAL64:
        out.type = TYPE_DECIMAL64;
        out.precision = type.precision;
        out.scale = type.scale;
        break;
    case TYPE_DECIMAL128:
        out.type = TYPE_DECIMAL128;
        out.precision = type.precision;
        out.scale = type.scale;
        break;
    case TYPE_JSON:
        out.type = TYPE_JSON;
        break;
    case TYPE_FUNCTION:
        out.type = TYPE_FUNCTION;
        break;
    case TYPE_VARBINARY:
        out.type = TYPE_VARBINARY;
        out.len = type.len;
        break;
    default:
        DCHECK(false) << "Unknown type: " << type;
    }
    return out;
}

} // namespace starrocks