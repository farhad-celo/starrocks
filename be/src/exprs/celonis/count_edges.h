#pragma once

#include "exprs/expr.h"
#include "exprs/expr_context.h"
#include "exprs/table_function/table_function.h"

namespace starrocks {

class CountEdges : public TableFunction {
public:
    std::pair<Columns, ColumnPtr> process(TableFunctionState* state, bool* eos) const override;

    Status init(const TFunction& fn, TableFunctionState** state) const override {
        *state = new TableFunctionState();
        return Status::OK();
    }

    Status prepare(TableFunctionState* state) const override { return Status::OK(); }

    Status open(RuntimeState* runtime_state, TableFunctionState* state) const override { return Status::OK(); };

    Status close(RuntimeState* runtime_state, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }
};

}  // namespace starrocks
