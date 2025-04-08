#pragma once

#include <queue>
#include <variant>

#include "column/datum.h"
#include "column/type_traits.h"
#include "exprs/celonis/row_buffer.h"
#include "exprs/celonis/sort_utils.h"
#include "types/logical_type.h"

namespace starrocks {
namespace celonis {

struct SorterMetadata {
    LogicalType logical_type;
    SortDescriptor sort_descriptor;
};

template <typename T>
concept TopKSupportedType = RowBufferSupportedType<T> && ComparableType<T>;

class TopKRowAccessor : public RowBufferRowAccessor {
public:
    TopKRowAccessor(ColumnIdxType row_size, RowIdxType row_idx) : RowBufferRowAccessor(row_size), row_idx_{row_idx} {}

    virtual ~TopKRowAccessor() = default;

    ColumnIdxType row_idx() const { return row_idx_; };

private:
    RowIdxType row_idx_;
};

/*
 * Class that represents a self-contained top-k priority queue for rows of arbitrary variadic row signatures.
 * Self-contained means that it stores the actual row values rather than just references to those values in an
 * internally maintained memory chunk. This allows this class to remember rows, even if the actual external row objects
 * are destructed (for example if the top k computation is distributed across different nodes).
 */
class TopKRowPriorityQueue {
public:
    /**
     * Creates a TopKRowPriorityQueue and initializes the internal row buffer.
     * @param size The maximum size of the priority queue.
     * @param sorters A description of the sorting config that determines the row signature as well as other properties.
     */
    TopKRowPriorityQueue(RowIdxType size, std::vector<SorterMetadata> sorters);

    /**
     * Tries to add a row to the TopKRowPriorityQueue. This can lead to another row being ejected if the internal priority queue is full.
     * If the priority queue is already full and the given row is ordered after all of them, then the given row is discarded and the state does not change.
     * The data within that row will be copied and serialized internally.
     * @param row Description of the input row including its index.
     */
    void try_push_row(const TopKRowAccessor& row);

    using RankingType = std::vector<RowIdxType>;

    /**
     * Given a TopKRowPriorityQueue this will return the current state of the internal priority queue, essentially the top-k
     * ranking of all rows that the sorter has seen at this point.
     * @return Integer vector of maximum length k. The indices of this vector are the ranks and the values are the row
     * indices of the seen row placed at this rank. Might be smaller than k, if less than k values were processed.
     */
    RankingType get_ranking() const;

    struct QueueEntry {
        RowIdxType row_idx;
        RowIdxType slot_id;
    };

private:
    struct Cmp {
        bool operator()(const QueueEntry& lhs, const QueueEntry& rhs);

        bool operator()(const TopKRowAccessor& lhs, const QueueEntry& rhs);

        std::shared_ptr<RowBuffer> buffer;
        std::shared_ptr<std::vector<SorterMetadata>> sorters;
    };

    std::shared_ptr<RowBuffer> value_buffer_;
    std::shared_ptr<std::vector<SorterMetadata>> sorters_;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, Cmp> queue_;
    RowIdxType size_;
};

} // namespace celonis
} // namespace starrocks