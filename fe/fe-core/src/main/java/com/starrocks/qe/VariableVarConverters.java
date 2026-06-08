// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package com.starrocks.qe;

import com.google.common.collect.Maps;
import com.starrocks.common.DdlException;
import com.starrocks.common.ErrorCode;
import com.starrocks.common.ErrorReport;

import java.util.Map;

// Helper class to drives the convert of session variables according to the converters.
// You can define your converter that implements interface VariableVarConverterI in here.
// Each converter should put in map (variable name -> converters) and only converts the variable
// with specified name.
public class VariableVarConverters {

    public static final Map<String, VariableVarConverterI> CONVERTERS = Maps.newHashMap();

    static {
        SqlModeConverter sqlModeConverter = new SqlModeConverter();
        CONVERTERS.put(SessionVariable.SQL_MODE, sqlModeConverter);
        PartialUpdateModeConverter partialUpdateModeConverter = new PartialUpdateModeConverter();
        CONVERTERS.put(SessionVariable.PARTIAL_UPDATE_MODE, partialUpdateModeConverter);
        CONVERTERS.put(SessionVariable.INSERT_MAX_FILTER_RATIO, new InsertMaxFilterRatioConverter());
        CONVERTERS.put(SessionVariable.MULTI_ARRAY_AGG_MAX_ARRAY_LENGTH, new MultiArrayAggMaxArrayLengthConverter());
    }

    public static String convert(String varName, String value) throws DdlException {
        if (CONVERTERS.containsKey(varName)) {
            return CONVERTERS.get(varName).convert(value);
        }
        return value;
    }

    /* Converters */

    // Converter to convert sql mode variable
    public static class SqlModeConverter implements VariableVarConverterI {
        @Override
        public String convert(String value) throws DdlException {
            return SqlModeHelper.encode(value).toString();
        }
    }

    // Converter to convert and check var `partial_update_mode`
    public static class PartialUpdateModeConverter implements VariableVarConverterI {
        @Override
        public String convert(String value) throws DdlException {
            if (value.equals("auto") || value.equals("row") || value.equals("column")) {
                return value;
            } else {
                throw new DdlException("partial_update_mode only support auto|row|column");
            }
        }
    }

    // Check var `insert_max_filter_ratio`
    public static class InsertMaxFilterRatioConverter implements VariableVarConverterI {
        @Override
        public String convert(String value) throws DdlException {
            try {
                double insertMaxFilterRatio = Double.parseDouble(value);
                if (insertMaxFilterRatio < 0 || insertMaxFilterRatio > 1) {
                    ErrorReport.reportDdlException(
                            ErrorCode.ERR_INVALID_VALUE, SessionVariable.INSERT_MAX_FILTER_RATIO, value, "between 0.0 and 1.0");
                }
            } catch (NumberFormatException e) {
                ErrorReport.reportDdlException(
                        ErrorCode.ERR_INVALID_VALUE, SessionVariable.INSERT_MAX_FILTER_RATIO, value, "between 0.0 and 1.0");
            }
            return value;
        }
    }

    // Check whether the session variable `multi_array_agg_max_array_length` falls into [0, 2^32]. 0 means "not enabled" -- in
    // this case the cluster-level config `array_agg_size_limit` is used. 2^32 is the upper bound. It matches the
    // Column::MAX_CAPACITY_LIMIT value in BE. Setting a value larger than it doesn't make sense.
    public static class MultiArrayAggMaxArrayLengthConverter implements VariableVarConverterI {
        public static final long MAX_VALUE = 4294967296L;
        @Override
        public String convert(String value) throws DdlException {
            try {
                long parsed = Long.parseLong(value);
                if (parsed < 0 || parsed > MAX_VALUE) {
                    ErrorReport.reportDdlException(ErrorCode.ERR_INVALID_VALUE, SessionVariable.MULTI_ARRAY_AGG_MAX_ARRAY_LENGTH,
                            value, "between 0 and 4294967296");
                }
            } catch (NumberFormatException e) {
                ErrorReport.reportDdlException(ErrorCode.ERR_INVALID_VALUE, SessionVariable.MULTI_ARRAY_AGG_MAX_ARRAY_LENGTH,
                        value, "between 0 and 4294967296");
            }
            return value;
        }
    }
}
