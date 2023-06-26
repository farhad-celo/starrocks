#include "replay_on_process_tree.h"

#include <algorithm>
#include <ranges>
#include <span>

#include "ctl/bits/half_open_interval.h"
#include "ctl/utility.h"
#include "modules/common/for_each_group.h"

namespace celonis::accelerator::operators::process {

namespace {

void zero_out(process_tree& tree) {
  // zero out the object count
  std::visit(
      ctl::overloaded{
          [](auto& c) { c.object_count = {}; },
          [](process_tree::exclusive& e) { std::ranges::fill(e.child_object_counts, process_tree::count_type{}); },
          [](process_tree::redo& r) {
            std::ranges::fill(r.child_redo_counts, process_tree::count_type{});
            r.object_count = {};
          },
      },
      tree.node);
  // recursively zero out the children's object counts
  std::visit(ctl::overloaded{
                 [](const process_tree::tau& /**/) { /* no children */ },
                 [](const process_tree::activity& /**/) { /* no children */ },
                 [](process_tree::parent& p) { std::ranges::for_each(p.children, zero_out); },
             },
             tree.node);
}

std::vector<row_id> activity_leaves(const process_tree& tree) {
  return std::visit(ctl::overloaded{[](const process_tree::parent& p) {
                                      std::vector<row_id> result{};
                                      for (const auto& child : p.children) {
                                        const auto child_leaves{activity_leaves(child)};
                                        result.insert(end(result), begin(child_leaves), end(child_leaves));
                                      }
                                      return result;
                                    },
                                    [](const process_tree::activity& a) { return std::vector{a.activity_id}; },
                                    [](process_tree::tau /**/) { return std::vector<row_id>{}; }},
                    tree.node);
}

template <typename ITERATOR>
[[maybe_unused]] ITERATOR extract_sub_activities(std::unordered_map<const process_tree*, std::span<const row_id>>& agg,
                                                 const process_tree& tree, ITERATOR first, ITERATOR last) {
  const auto* ptr = &tree;
  return std::visit(
      ctl::overloaded{
          [first](process_tree::tau /**/) { return first; },
          [first, last, ptr, &agg](const process_tree::activity& a) {
            if (first == last || a.activity_id != *first) {
              throw common::cpm_exception{
                  "replay_on_process_tree: extracted flat activity buffer does not match with the process tree"};
            }
            const auto next{std::next(first)};
            agg.try_emplace(ptr, first, next);
            return next;
          },
          [first, last, ptr, &agg](const process_tree::parent& p) {
            auto next{first};
            for (const auto& child : p.children) {
              next = extract_sub_activities(agg, child, next, last);
            }
            agg.try_emplace(ptr, first, next);
            return next;
          }},
      tree.node);
}

template <typename ITERATOR>
std::unordered_map<const process_tree*, std::span<const row_id>> sub_activities(process_tree& tree, ITERATOR first,
                                                                                ITERATOR last) {
  // NB process trees that are not present here may be assumed to be empty (i.e., exclusively accept the empty word)
  std::unordered_map<const process_tree*, std::span<const row_id>> result{};

  extract_sub_activities(result, tree, first, last);

  return result;
}

template <typename ACCESSOR>
struct process_tree_replayer {
  process_tree_replayer(process_tree& tree, ACCESSOR activity_accessor,
                        const cube::filter_bitset_t& eventlog_selections)
      : tree_{tree},
        activity_accessor_{std::move(activity_accessor)},
        eventlog_selections_{eventlog_selections},
        activity_buffer_(activity_leaves(tree)),
        sub_activities_(sub_activities(tree, begin(activity_buffer_), end(activity_buffer_))) {}
  /**
   * Replay a single trace, and record the result in the replay_buffer_ if successful, or else in failed_indices.
   * @tparam INDEX The type used for indexing the trace_buffer_
   * @param interval One trace of the event-log
   */
  template <typename INDEX>
  void operator()(ctl::half_open_interval<INDEX> interval) {
    // fill buffer
    trace_buffer_.clear();
    for (auto i{interval.begin()}; i != interval.end(); ++i) {
      if (eventlog_selections_.test(i)) {
        trace_buffer_.emplace_back(activity_accessor_[i]);
      }
    }
    // replay trace and record result
    if (trace_buffer_.empty()) {
      return;  // ignored case
    }
    if (replay()) {
      std::ranges::for_each(replay_buffer_, [this](auto* ptr) { ++node_counts_[ptr]; });
    } else {  // failed to replay, record
      failed_indices_.emplace_back(interval.begin());
    }
  }
  std::unordered_map<process_tree::count_type*, row_id> counts() const { return node_counts_; }
  std::vector<row_id> failed_indices() const { return failed_indices_; }

 private:
  process_tree& tree_;
  ACCESSOR activity_accessor_;
  const cube::filter_bitset_t& eventlog_selections_;
  std::vector<row_id> activity_buffer_;
  std::unordered_map<const process_tree*, std::span<const row_id>> sub_activities_;
  std::unordered_map<process_tree::count_type*, row_id> node_counts_{};
  std::vector<row_id> failed_indices_{};
  std::vector<row_id> trace_buffer_{};
  std::vector<process_tree::count_type*> replay_buffer_{};

  template <std::contiguous_iterator ITERATOR>
  ITERATOR end_of_subactivities(const process_tree* ptr, ITERATOR first, ITERATOR last) {
    if (auto it{sub_activities_.find(ptr)}; it != end(sub_activities_)) {
      return std::find_if(first, last, [activities = it->second](auto a) {
        return std::ranges::find(activities, a) == end(activities);
      });
    }
    return first;
  }

  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(const process_tree* /*wrapper*/, process_tree::tau& t, ITERATOR first,
                                           ITERATOR least, ITERATOR /*most*/) {
    if (first != least) {
      return std::nullopt;
    }
    replay_buffer_.emplace_back(&t.object_count);
    return std::optional{least};
  }

  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(const process_tree* /*wrapper*/, process_tree::activity& a, ITERATOR first,
                                           ITERATOR least, ITERATOR most) {
    if (first == most || std::distance(first, least) > 1 || a.activity_id != *first) {
      return std::optional<ITERATOR>{};
    }
    replay_buffer_.emplace_back(&a.object_count);
    return std::optional{std::next(first)};
  }

  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(const process_tree* /*wrapper*/, process_tree::exclusive& e, ITERATOR first,
                                           ITERATOR least, ITERATOR most) {
    const auto extract_tau_from_children{[this, first](auto& r) {
      return std::ranges::find_if(
          r, [this, first](auto& child) { return this->extract_at_least(child, first, first, first).has_value(); });
    }};
    if (first == most) {  // extract the empty word from any child
      if (const auto it{extract_tau_from_children(e.children)}; it != end(e.children)) {
        replay_buffer_.emplace_back(std::next(e.child_object_counts.data(), std::distance(begin(e.children), it)));
        return std::optional{first};
      }
      return std::optional<ITERATOR>{};
    }
    // locate the correct child, then greedily extract from it
    const auto child_it{std::ranges::find_if(e.children, [this, a = *first](auto& child) {
      const auto it{sub_activities_.find(&child)};
      return it != end(sub_activities_) && std::ranges::find(it->second, a) != end(it->second);
    })};
    if (child_it != end(e.children)) {
      if (auto result{extract_at_least(*child_it, first, least, most)}) {
        replay_buffer_.emplace_back(
            std::next(e.child_object_counts.data(), std::distance(begin(e.children), child_it)));
        return result;
      }
    }
    // fallback: try to extract tau
    if (first == least) {
      if (const auto it{extract_tau_from_children(e.children)}; it != end(e.children)) {
        replay_buffer_.emplace_back(std::next(e.child_object_counts.data(), std::distance(begin(e.children), it)));
        return std::optional{first};
      }
    }
    return std::optional<ITERATOR>{};
  }

  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(const process_tree* /*wrapper*/, process_tree::sequence& s, ITERATOR first,
                                           ITERATOR least, ITERATOR most) {
    std::optional result{first};
    const auto initial_replay_buffer_size{replay_buffer_.size()};  // in case we need to roll back
    for (auto& child : s.children) {
      result = extract_at_least(child, *result, *result, most);
      if (!result) {
        replay_buffer_.resize(initial_replay_buffer_size);
        return std::optional<ITERATOR>{};
      }
    }
    if (std::distance(*result, least) > 0) {
      replay_buffer_.resize(initial_replay_buffer_size);
      return std::optional<ITERATOR>{};
    }
    replay_buffer_.emplace_back(&s.object_count);
    return result;
  }

  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(const process_tree* wrapper, process_tree::parallel& p, ITERATOR first,
                                           ITERATOR least, ITERATOR most) {
    // for each child, stably partition the copy, then try to extract everything
    // repeat with most <- prev(most), until all children extract successfully, or most == least
    // TODO(a.swoboda) this is less efficient than it could be
    auto last{end_of_subactivities(wrapper, first, most)};
    const auto initial_replay_buffer_size{replay_buffer_.size()};
    while (least <= last) {
      std::vector cp(first, last);  // TODO(a.swoboda) pop_back instead of re-constructing each time
      std::optional current{begin(cp)};
      for (auto& child : p.children) {
        const auto sub_activities_it{sub_activities_.find(&child)};
        const auto child_last{
            sub_activities_it == end(sub_activities_)
                ? *current
                : std::stable_partition(*current, end(cp), [activities = sub_activities_it->second](auto activity) {
                    return std::ranges::find(activities, activity) != end(activities);
                  })};
        // replay child, trying to extract everything
        current = extract_at_least(child, *current, child_last, child_last);
        if (!current) {
          // roll back
          replay_buffer_.resize(initial_replay_buffer_size);
          break;
        }
      }
      // check if we successfully replayed everything
      if (current && current.value() == end(cp)) {
        replay_buffer_.emplace_back(&p.object_count);
        return std::optional{last};
      }
      // else, we remove the last and try again
      replay_buffer_.resize(initial_replay_buffer_size);
      --last;
    }
    // exiting the loop means that we haven't found anything
    return std::optional<ITERATOR>{};
  }

  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(const process_tree* /*wrapper*/, process_tree::redo& r, ITERATOR first,
                                           ITERATOR least, ITERATOR most) {
    const auto initial_replay_buffer_size{replay_buffer_.size()};  // for rolling back
    // extract do part
    auto candidate{
        extract_at_least(r.children.front(), first, first, end_of_subactivities(r.children.data(), first, most))};
    if (!candidate) {
      return std::optional<ITERATOR>{};
    }
    replay_buffer_.emplace_back(r.child_redo_counts.data());
    while (candidate && *candidate != most) {  // there may be more to extract
      // NB we need to track progress, as we do not want to extract taus indefinitely,
      //  i.e., after one full loop, we must have made some progress
      const auto current_replay_buffer_size{replay_buffer_.size()};
      // locate redo child
      auto redo_child_it{std::find_if(std::next(begin(r.children)), end(r.children), [this, candidate](auto& child) {
        return this->end_of_subactivities(&child, *candidate, std::next(*candidate)) != *candidate;
      })};
      // if redo child found, try to replay it
      std::optional<ITERATOR> after_redo{};
      if (redo_child_it != end(r.children)) {
        after_redo = extract_at_least(*redo_child_it, *candidate, *candidate, most);
      }
      // else, try to replay the empty word on any redo child
      if (!after_redo.has_value()) {
        redo_child_it = std::find_if(std::next(begin(r.children)), end(r.children), [this, candidate](auto& child) {
          return this->extract_at_least(child, *candidate, *candidate, *candidate).has_value();
        });
        if (redo_child_it != end(r.children)) {
          after_redo = candidate;  // we found an accepting child, but didn't extract anything
        }
      }
      if (!after_redo.has_value()) {
        // roll back this iteration's changes
        replay_buffer_.resize(current_replay_buffer_size);
        break;
      }
      replay_buffer_.emplace_back(
          std::next(r.child_redo_counts.data(), std::distance(begin(r.children), redo_child_it)));

      // Now we are finally ready to try and replay the do part
      auto after_do{extract_at_least(r.children.front(), *after_redo, *after_redo,
                                     end_of_subactivities(r.children.data(), *after_redo, most))};
      if (!after_do.has_value() || *after_do == *candidate) {
        replay_buffer_.resize(current_replay_buffer_size);
        break;
      }
      replay_buffer_.emplace_back(r.child_redo_counts.data());
      candidate = after_do;
    }
    // if the result is smaller than least, then roll back and return nothing
    if (!candidate.has_value() || std::distance(*candidate, least) > 0) {
      replay_buffer_.resize(initial_replay_buffer_size);
      return std::optional<ITERATOR>{};
    }
    // else, this was a success.
    replay_buffer_.emplace_back(&r.object_count);
    return candidate;
  }

  /**
   * Replay part of a trace on a process (sub-)tree.
   *
   * Replaying a trace can be done by allocating the activities to the respective sub-trees. There are a few cases where
   * this allocation is not trivial, though, like redo nodes with children that can accept the empty word, or
   * combinations of parallel and redo nodes. In these cases, there can be different allocations that one needs to try.
   * Here, we do this by backtracking. We always start by trying to extract as many activities as possible from a trace,
   * and on failure, we roll back and try to extract less activities.
   *
   * @tparam ITERATOR The type of iterator pointing to trace elements
   * @param tree The process tree to replay on
   * @param first The beginning of the (sub-)trace
   * @param least The minimal element to extract
   * @param most The end of the (sub-)trace (iterator past the maximal element to extract)
   * @return An iterator to the first activity that we couldn't replay, or an empty optional if we couldn't replay to
   *  somewhere between 'least' and 'most' activities
   */
  template <std::contiguous_iterator ITERATOR>
  std::optional<ITERATOR> extract_at_least(process_tree& tree, ITERATOR first, ITERATOR least, ITERATOR most) {
    // at the top level, least and most have to be the same, which is the end of the trace (first is the begin-ning)
    return std::visit([first, least, most, this,
                       wrapper = &tree](auto& n) { return this->extract_at_least(wrapper, n, first, least, most); },
                      tree.node);
  }

  bool replay() {
    replay_buffer_.clear();
    return extract_at_least(tree_, begin(trace_buffer_), end(trace_buffer_), end(trace_buffer_)).has_value();
  }
};

}  // namespace

process_tree replay_on_process_tree(process_tree tree, const memory::column_t& activity_column,
                                    const memory::column_t& case_column,
                                    const cube::filter_bitset_t& eventlog_selections,
                                    const common::execution_context& context) {
  debug_assert(activity_column->get_row_count(context) == case_column->get_row_count(context));
  debug_assert(ctl::cast<size_t>(activity_column->get_row_count(context)) == eventlog_selections.size());
  zero_out(tree);
  const auto increments{memory::cast_execute_column_pointers(
      [&tree, &eventlog_selections](auto tup) {
        process_tree_replayer replayer{tree, std::get<0>(tup).get_const_accessor(), eventlog_selections};
        // TODO(a.swoboda) parallelize
        // TODO(a.swoboda) Can we use variants for this?
        common::for_each_group(std::get<1>(tup).get_const_accessor(), std::ref(replayer));
        return replayer.counts();
      },
      activity_column->get_column_pointers(context), case_column->get_column_pointers(context))};
  std::ranges::for_each(increments, [](auto p) { *p.first += p.second; });
  return tree;
}

}  // namespace celonis::accelerator::operators::process
