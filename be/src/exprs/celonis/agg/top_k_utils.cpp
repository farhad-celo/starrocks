#include "top_k_utils.h"

#include "exec/sorting/sort_helper.h"
#include "types/logical_type_infra.h"

namespace starrocks {
namespace celonis {

namespace {

Datum get_datum(const TopKRowAccessor& row_accessor, ColumnIdxType column_idx, const RowBuffer& /*buffer*/) {
    return row_accessor.get(column_idx);
}

Datum get_datum(const TopKRowPriorityQueue::QueueEntry& entry, ColumnIdxType column_idx, const RowBuffer& buffer) {
    return buffer.get(entry.slot_id, column_idx);
}

RowIdxType get_row_idx(const TopKRowAccessor& row_accessor) {
    return row_accessor.row_idx();
}

RowIdxType get_row_idx(const TopKRowPriorityQueue::QueueEntry& entry) {
    return entry.row_idx;
}

template <typename LHS_TYPE, typename RHS_TYPE>
bool cmp(const LHS_TYPE& lhs, const RHS_TYPE& rhs, const RowBuffer& buffer,
         const std::vector<SorterMetadata>& sorters) {
    DCHECK(buffer.num_columns() == sorters.size());
    for (ColumnIdxType i{0}; i < buffer.num_columns(); i++) {
        Datum lhs_datum{get_datum(lhs, i, buffer)};
        Datum rhs_datum{get_datum(rhs, i, buffer)};
        CmpResult cmp_result{DatumComparator{}(lhs_datum, rhs_datum, sorters[i].sort_descriptor)};
        if (cmp_result == CmpResult::EQUAL) {
            continue;
        }
        return cmp_result == CmpResult::LESS;
    }

    return get_row_idx(lhs) < get_row_idx(rhs);
}

std::vector<LogicalType> extract_type_signature(const std::vector<SorterMetadata>& sorter_metadata) {
    std::vector<LogicalType> type_signature{};
    type_signature.reserve(sorter_metadata.size());
    for (const SorterMetadata& metadata : sorter_metadata) {
        type_signature.emplace_back(metadata.logical_type);
    }
    return type_signature;
}

} // namespace

TopKRowPriorityQueue::TopKRowPriorityQueue(RowIdxType size, std::vector<SorterMetadata> sorters)
        : value_buffer_{std::make_shared<RowBuffer>(size, extract_type_signature(sorters))},
          sorters_{std::make_shared<std::vector<SorterMetadata>>(std::move(sorters))},
          queue_{TopKRowPriorityQueue::Cmp{.buffer = value_buffer_, .sorters = sorters_}},
          size_{size} {
    DCHECK(value_buffer_->num_columns() == sorters_->size());
}

bool TopKRowPriorityQueue::Cmp::operator()(const QueueEntry& lhs, const QueueEntry& rhs) {
    return cmp(lhs, rhs, *buffer, *sorters);
}

bool TopKRowPriorityQueue::Cmp::operator()(const TopKRowAccessor& lhs, const QueueEntry& rhs) {
    DCHECK(buffer->num_columns() == lhs.num_columns());
    return cmp(lhs, rhs, *buffer, *sorters);
}

void TopKRowPriorityQueue::try_push_row(const TopKRowAccessor& row) {
    if (queue_.size() < size_) {
        RowIdxType next_free_slot{queue_.size()};
        value_buffer_->set(next_free_slot, row);
        queue_.push(QueueEntry{.row_idx = row.row_idx(), .slot_id = next_free_slot});
    } else {
        const QueueEntry& largest_element{queue_.top()};
        // If the row is ordered after the last element of the current top k anyway we do not need to add it
        if (!Cmp{.buffer = value_buffer_, .sorters = sorters_}(row, largest_element)) {
            return;
        }
        RowIdxType slot_id_to_reuse{largest_element.slot_id};
        queue_.pop();
        value_buffer_->set(slot_id_to_reuse, row);
        queue_.push(QueueEntry{.row_idx = row.row_idx(), .slot_id = slot_id_to_reuse});
    }
}

TopKRowPriorityQueue::RankingType TopKRowPriorityQueue::get_ranking() const {
    RankingType result_ranking(queue_.size());
    auto copied_queue{queue_};
    for (RowIdxType k{copied_queue.size()}; k > 0; k--) {
        result_ranking[k - 1] = (copied_queue.top().row_idx);
        copied_queue.pop();
    }
    return result_ranking;
}

} // namespace celonis
} // namespace starrocks