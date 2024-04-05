#pragma once

#include <boost/dynamic_bitset.hpp>

// this code is ported from Saola

namespace celonis::accelerator::cube {

/* Takes the start and end idx of a single day in an array sorted by timestamp and rewrites the timestamps based on
 * the rules for day-based activities. */
template <class SORTING_COMPARATOR, class TIMESTAMP_REWRITER, class IS_DAY_BASED_EXTRACTOR>
void reorder_single_day(int64_t day_start_idx, int64_t day_end_idx, const SORTING_COMPARATOR& sorting_cmp,
                        const TIMESTAMP_REWRITER& rewrite_timestamps, const IS_DAY_BASED_EXTRACTOR& is_day_based) {
    // We keep track of the last index so as to continue our search from where we left off (prevents N^2 complexity)
    int64_t last_smaller_sorting_idx{-1};
    int64_t last_greater_sorting_idx{-1};

    // Initialize the last_*_idx indices as the index of the *first* time based activity
    for (int64_t row{day_start_idx}; row < day_end_idx; row++) {
        if (!is_day_based(row)) {
            last_smaller_sorting_idx = row;
            last_greater_sorting_idx = row;
            break;
        }
    }

    // If no time based activity is present in the given [start, end] range, then there is no need to reorder
    if (last_greater_sorting_idx == -1) {
        return;
    }

    // Otherwise we can do the reordering
    for (int64_t row{day_start_idx}; row < day_end_idx; row++) {
        if (!is_day_based(row)) {
            continue;
        }

        auto day_based_activity{row};

        // This is a day based activity, so we need to reorder this one
        int64_t smaller_sorting_idx{-1};
        int64_t greater_sorting_idx{-1};

        // Search for the *first* non day based activity with *greater* sorting value
        for (int64_t non_day_based_activity{last_greater_sorting_idx}; non_day_based_activity < day_end_idx;
             non_day_based_activity++) {
            if (is_day_based(non_day_based_activity)) {
                continue;
            }

            if (sorting_cmp(day_based_activity, non_day_based_activity)) {
                // day_based_activity is smaller than time_based_activity => time_based_activity is an activity with a
                // greater sorting than day_based_activity
                greater_sorting_idx = non_day_based_activity;
                last_greater_sorting_idx = greater_sorting_idx;
                break;
            }
        }

        // Search for the time based activity with the *greatest smaller* sorting value
        for (int64_t non_day_based_activity{last_smaller_sorting_idx}; non_day_based_activity < day_end_idx;
             non_day_based_activity++) {
            if (is_day_based(non_day_based_activity)) {
                continue;
            }
            if (sorting_cmp(non_day_based_activity, day_based_activity)) {
                // non_day_based_activity is smaller than day_based_activity => non_day_based_activity is an activity with a
                // smaller sorting than day_based_activity
                smaller_sorting_idx = non_day_based_activity;
                last_smaller_sorting_idx = smaller_sorting_idx;
            } else {
                break;
            }
        }

        // Assign timestamps
        if (smaller_sorting_idx == -1 && greater_sorting_idx != -1) {
            rewrite_timestamps(day_based_activity, greater_sorting_idx);
        } else if (smaller_sorting_idx != -1) {
            // This case covers:
            // 1) smaller_sorting_idx is found, but greater_sorting_idx is not found
            // 2) Both smaller_sorting_idx and greater_sorting_idx are found: If we have a conflict, this resolves the
            //    conflict by putting the activity behind the last activity with greater sorting.
            rewrite_timestamps(day_based_activity, smaller_sorting_idx);
        }
    }
}

} // namespace celonis::accelerator::cube
