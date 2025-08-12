#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMoBpmnGraph {
public:
    /**
     * @param: [process_tree [, ..., process_tree]]
     * @paramType columns: [VARCHAR]
     * @return: VARCHAR
     * process_tree : The result of celonis_inductive_miner().
     * Supports PQL MO_BPMN_GRAPH https://confluence.celonis.com/display/PQLdevelopment/MO_BPMN_GRAPH+Query
     * It does not support strategy which is being deprecated.
     */
    DEFINE_VECTORIZED_FN(mo_bpmn_graph);
};

} // namespace starrocks