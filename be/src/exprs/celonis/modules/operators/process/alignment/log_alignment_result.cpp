#include "log_alignment_result.h"

#include <numeric>
#include <vector>

#include <bytell_hash_map.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/assert.h"
#include "modules/common/int_types.h"
#include "modules/memory/builders/cache_column_from_dictionary.h"
#include "modules/memory/builders/result_column_builder.h"
#include "modules/memory/column.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::process::alignment {

namespace {

constexpr size_t MOVE_REPRESENTATION_SIZE{3};

constexpr row_id LOG_MOVE_POINTER_OFFSET{1};
constexpr row_id MODEL_MOVE_POINTER_OFFSET{2};
constexpr row_id SYNC_MOVE_POINTER_OFFSET{3};
constexpr row_id UNMAPPED_MOVE_POINTER_OFFSET{4};

constexpr size_t LOG_MOVE_BUFFER_OFFSET{0};
constexpr size_t MODEL_MOVE_BUFFER_OFFSET{MOVE_REPRESENTATION_SIZE + 1};
constexpr size_t SYNC_MOVE_BUFFER_OFFSET{2 * (MOVE_REPRESENTATION_SIZE + 1)};
constexpr size_t UNMAPPED_MOVE_BUFFER_OFFSET{3 * (MOVE_REPRESENTATION_SIZE + 1)};

constexpr size_t NUMBER_OF_MOVES{4};

/**
 * Returns for a pair of [variable_id, transition_id] the string buffer entry.
 *  Passed to exec_expand_alignments as strategy on how to fill output column
 */
struct mapper_to_move_buffer {
  [[nodiscard]] constexpr row_id operator()(const alignment_move& move) const {
    switch (move.type()) {
      using enum alignment_move_type;
      case MODEL:
        return MODEL_MOVE_POINTER_OFFSET;
      case LOG:
        return LOG_MOVE_POINTER_OFFSET;
      case SYNC:
        return SYNC_MOVE_POINTER_OFFSET;
      case UNMAPPED:
        return UNMAPPED_MOVE_POINTER_OFFSET;
      default:
        legacy_embedded_ctl::assert_unreachable();
    }
  }
};

/**
 * Returns for a pair of [variable_id, transition_id] the dictionary entry.
 *  Passed to exec_expand_alignments as strategy on how to fill output column
 */
class mapper_to_activity_buffer {
 public:
  explicit mapper_to_activity_buffer(ska::bytell_hash_map<row_id, row_id> int_to_buffer_entry)
      : int_to_buffer_entry{std::move(int_to_buffer_entry)} {}

  [[nodiscard]] row_id operator()(const alignment_move& move) const { return int_to_buffer_entry.at(move.label()); }

 private:
  ska::bytell_hash_map<row_id, row_id> int_to_buffer_entry;
};

template <class TUPLE, class OUTPUT_MAPPER_OBJ, class PROJECTION_VECTOR>
class exec_execute_fill_output {
 public:
  exec_execute_fill_output(row_id output_table_size, row_id case_table_size, const vector_of_alignments& alignments,
                           const TUPLE& t, const OUTPUT_MAPPER_OBJ&& output_mapper,
                           const PROJECTION_VECTOR& projection_vector, const common::execution_context& context)
      : output_table_size{output_table_size},
        case_table_size{case_table_size},
        alignments{alignments},
        t{t},
        output_mapper{std::move(output_mapper)},
        projection_vector{std::move(projection_vector)},
        context{context} {}

  template <class COL_PTRS_TYPE>
  memory::raw_column_ptrs_t operator()() {
    auto raw_column_pointers{
        memory::create_raw_column_pointer<COL_PTRS_TYPE>(output_table_size, memory::zero_init_t{true}, context)};
    auto output_ptrs_ac{raw_column_pointers->get_data()};
    size_t output_col_index{0};

    const auto variant_column_ac{std::get<0>(t).get_const_accessor()};
    row_id previous_projected_case{-1};

    for (row_id i{0}; i < case_table_size; ++i) {
      const auto current_projected_case{projection_vector[i]};
      if (current_projected_case != previous_projected_case && current_projected_case != VALUE_NOT_FOUND) {
        // New case starts. Get the corresponding alignment
        previous_projected_case = current_projected_case;
        const auto variant_id{variant_column_ac[current_projected_case]};
        if (variant_id == 0 || !alignments.at(variant_id).has_value()) {
          continue;
        }
        const auto& variant_alignment{alignments.at(variant_id).value()};

        for (const auto& move : variant_alignment.data()) {
          legacy_embedded_debug_assert(output_col_index < static_cast<size_t>(output_table_size));
          output_ptrs_ac[output_col_index] = static_cast<COL_PTRS_TYPE>(output_mapper(move));
          ++output_col_index;
        }
        i += static_cast<row_id>(variant_alignment.size() - variant_alignment.cost()) - 1;
      }
    }

    return raw_column_pointers;
  }

 private:
  const row_id output_table_size;
  const row_id case_table_size;
  const vector_of_alignments& alignments;
  const TUPLE& t;
  [[no_unique_address]] const OUTPUT_MAPPER_OBJ output_mapper;
  const PROJECTION_VECTOR& projection_vector;
  const common::execution_context& context;
};

template <bool IS_ACTIVITY>
class exec_expand_alignments {
 public:
  exec_expand_alignments(const row_id output_table_size, const row_id activity_table_size,
                         const vector_of_alignments& alignments, const string_to_int_mapper& str_mapper,
                         memory::join_projection_vector_t projection_vector, const common::execution_context& context)
      : output_table_size{output_table_size},
        activity_table_size{activity_table_size},
        alignments{alignments},
        str_mapper{str_mapper},
        projection_vector{std::move(projection_vector)},
        context{context} {}

  template <class TUPLE>
  memory::builders::result_column_builder_t operator()(const TUPLE& t) {
    if constexpr (IS_ACTIVITY) {
      return execute_for_buffer_generator(t, [this]() { return get_activity_string_dictionary(); });
    }

    return execute_for_buffer_generator(t, [this]() { return get_move_string_dictionary(); });
  }

 private:
  template <class TUPLE, class STR_BUFFER_GENERATION_LMB>
  memory::builders::result_column_builder_t execute_for_buffer_generator(
      const TUPLE& t, const STR_BUFFER_GENERATION_LMB&& str_buffer_generator_lmb) {
    auto [str_buffer, str_pointers, output_mapper]{str_buffer_generator_lmb()};

    auto output_column_pointers{memory::cast_execute_projection_vector(
        [this, &t, &output_mapper = output_mapper,
         size = static_cast<row_id>(str_pointers.size())](const auto& projection_vec) {
          return memory::execute_with_column_pointers_type(
              exec_execute_fill_output{output_table_size, activity_table_size, alignments, t, std::move(output_mapper),
                                       projection_vec, context},
              size);
        },
        projection_vector)};

    return memory::builders::cache_column_from_dictionary::builder::get()
        ->set_raw_column_pointer(std::move(output_column_pointers))
        ->set_raw_dictionary(std::move(str_pointers))
        ->set_string_buffer(std::move(str_buffer))
        ->build();
  }

  [[nodiscard]] auto get_activity_string_dictionary() const {
    std::vector<std::pair<row_id, std::string>> str_mapper_entries{};
    str_mapper_entries.reserve(str_mapper.get_data().size());

    // Copy all but null entries, which will be copied and handled later
    std::ranges::copy_if(str_mapper.get_data(), std::back_inserter(str_mapper_entries),
                         [](const auto& entry) { return entry.first != 0; });

    std::ranges::sort(str_mapper_entries, [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

    size_t str_buffer_size{std::accumulate(
        std::cbegin(str_mapper_entries), std::cend(str_mapper_entries), size_t{0},
        [](size_t partial_result, const auto& entry) { return partial_result + entry.second.size() + 1; })};
    str_buffer_size += NULL_STRING.size();

    auto activity_string_buffer{memory::tracking::make_static_array_for_overwrite<char>(
        str_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

    // Add 1 to account for the nullptr
    // It's safe to cast from size_t to row_id here because at this point it was already checked
    //  whether the output column size fits into row_id type. Therefore also int_to_buffer_entry.size() fits into it
    const row_id activity_string_pointers_size{static_cast<row_id>(str_mapper_entries.size() + 1)};
    auto activity_string_pointers{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
        activity_string_pointers_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

    ska::bytell_hash_map<row_id, row_id> int_to_buffer_entry;
    int_to_buffer_entry.reserve(activity_string_pointers_size);

    // Add NULL string to beginning of buffer and pointers
    auto* str_buffer_ptr{activity_string_buffer.data()};
    activity_string_pointers[0] = str_buffer_ptr;
    std::strncpy(str_buffer_ptr, NULL_STRING.data(), NULL_STRING.size());
    str_buffer_ptr += NULL_STRING.size();
    row_id current_buffer_pointer{1};

    for (const auto& [activity_id, activity_string] : str_mapper_entries) {
      // Add the pointer entry
      int_to_buffer_entry.emplace(activity_id, current_buffer_pointer);
      activity_string_pointers[current_buffer_pointer] = str_buffer_ptr;
      current_buffer_pointer++;

      // Copy the string to the buffer
      std::strncpy(str_buffer_ptr, activity_string.c_str(), activity_string.size());
      str_buffer_ptr += activity_string.size();
      *str_buffer_ptr = '\0';
      str_buffer_ptr += 1;
    }

    return std::make_tuple(std::move(activity_string_buffer), std::move(activity_string_pointers),
                           mapper_to_activity_buffer{std::move(int_to_buffer_entry)});
  }

  [[nodiscard]] auto get_move_string_dictionary() const {
    constexpr size_t str_buffer_size{NULL_STRING.size() + (NUMBER_OF_MOVES * (MOVE_REPRESENTATION_SIZE + 1))};

    auto str_buffer{memory::tracking::make_static_array_for_overwrite<char>(
        str_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
    // Add 1 to account for the nullptr
    static constexpr const row_id move_string_pointers_size{NUMBER_OF_MOVES + 1};
    auto move_str_pointer{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
        move_string_pointers_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

    // String Buffer of moves consists of this 3 strings and NULL
    std::strncpy(str_buffer.data(), NULL_STRING.data(), NULL_STRING.size());
    static constexpr const char* BUFFER_CONTENT{"[L]\0[M]\0[S]\0[X]\0"};
    std::copy(BUFFER_CONTENT, BUFFER_CONTENT + NUMBER_OF_MOVES * (MOVE_REPRESENTATION_SIZE + 1),
              str_buffer.data() + NULL_STRING.size());

    move_str_pointer[0] = str_buffer.data();
    move_str_pointer[1] = str_buffer.data() + NULL_STRING.size() + LOG_MOVE_BUFFER_OFFSET;
    move_str_pointer[2] = str_buffer.data() + NULL_STRING.size() + MODEL_MOVE_BUFFER_OFFSET;
    move_str_pointer[3] = str_buffer.data() + NULL_STRING.size() + SYNC_MOVE_BUFFER_OFFSET;
    move_str_pointer[4] = str_buffer.data() + NULL_STRING.size() + UNMAPPED_MOVE_BUFFER_OFFSET;

    return std::make_tuple(std::move(str_buffer), std::move(move_str_pointer), mapper_to_move_buffer{});
  }

  const row_id output_table_size;
  const row_id activity_table_size;
  const vector_of_alignments& alignments;
  const string_to_int_mapper& str_mapper;
  const memory::join_projection_vector_t projection_vector;
  const common::execution_context& context;
};

}  // namespace

memory::builders::result_column_builder_t log_alignment_result::align_activity(
    row_id output_table_size, row_id activity_table_size, const common::execution_context& context,
    const memory::column_t& variant_column) const {
  return memory::cast_execute_column_pointers(
      exec_expand_alignments<true>(output_table_size, activity_table_size, alignments_, str_mapper_, projection_vector_,
                                   context),
      variant_column->get_column_pointers(context));
}

memory::builders::result_column_builder_t log_alignment_result::align_move(
    row_id output_table_size, row_id activity_table_size, const common::execution_context& context,
    const memory::column_t& variant_column) const {
  return memory::cast_execute_column_pointers(
      exec_expand_alignments<false>(output_table_size, activity_table_size, alignments_, str_mapper_,
                                    projection_vector_, context),
      variant_column->get_column_pointers(context));
}

}  // namespace celonis::accelerator::operators::process::alignment
