#pragma once

#include <bytell_hash_map.hpp>
#include <limits>
#include <memory>
#include <numeric>
#include <type_traits>
#include <unordered_map>

#include "modules/common/buffer_types.h"
#include "modules/common/execution_context.h"
#include "modules/memory/builders/temp_column_builder.h"
#include "modules/memory/column.h"
#include "modules/memory/table.h"
#include "utils/nullable_pql_value.h"

namespace celonis::accelerator {

// TODO(n.weber): move impl into .cpp to reduce compile time
template <typename T>
class column_builder {
public:
    column_builder() = default;

    column_builder& owner(memory::table* new_owner) {
        owner_ = new_owner;
        return *this;
    }

    column_builder& size(int32_t size) { return column_builder::size(static_cast<int64_t>(size)); }

    column_builder& size(int64_t size) {
        if (size < 0) {
            throw std::runtime_error{"Size is negative."};
        }
        return column_builder::size(static_cast<size_t>(size));
    }

    column_builder& size(const size_t size) {
        if (size > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
            throw std::runtime_error{"Size is too large."};
        }
        auto row_id_size = static_cast<row_id>(size);
        if (data_set_) {
            throw std::runtime_error{"Column data is already set."};
        }
        if (size_set_) {
            if (data_size_ != row_id_size) {
                throw std::runtime_error{"Data size does not match stated size."};
            }
        } else {
            data_size_ = row_id_size;
            data_array_ = legacy_embedded_ctl::make_static_array_for_overwrite<T>(
                    legacy_embedded_ctl::cast<size_t>(size),
                    LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG));
            null_flags_ = memory::create_null_flags(size, common::execution_context{});
            size_set_ = true;
        }
        return *this;
    }

    [[nodiscard]] row_id size() const {
        if (!size_set_) {
            throw common::internal_exception{"Size has not been set yet."};
        }
        return data_size_;
    }

    template <typename... TS>
    column_builder& data(TS... input) {
        return data(utils::nullable_vec_t<T>{input...});
    }

    /**
   * @brief sets both data and null flags.
   */
    column_builder& data(const utils::nullable_vec_t<T>& input) {
        size(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            data_array_[i] = input[i].get_or_default();
            null_flags_->set(i, input[i].is_null());
        }
        data_set_ = true;
        return *this;
    }

    column_builder& data(const std::vector<T>& input) {
        size(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            data_array_[i] = input[i];
            null_flags_->set(i, false);
        }
        data_set_ = true;
        return *this;
    }

    /**
   * Generates the column data using a functor. The size needs to be specified in advance. Both the data *and* the
   * null flags can be initialized with this function.
   *
   * @param f A function with a single argument (the row index) which should return a type that is convertible
   *        to a nullable_pql_value<T>.
   */
    template <typename FUNCTOR>
    column_builder& generate_data(FUNCTOR f) {
        if (data_set_) {
            throw std::runtime_error{"Column data is already set."};
        }
        if (!size_set_) {
            throw std::runtime_error{"Size needs to be set for data to be generated."};
        }

        for (row_id i = 0; i < data_size_; ++i) {
            // Not using {}-init to allow narrowing conversion (for now, as it affects a lot of tests)
            utils::nullable_pql_value<T> value(f(i));
            data_array_[i] = value.get_or_default();
            null_flags_->set(i, value.is_null());
        }
        data_set_ = true;
        return *this;
    }

    column_builder& name(std::string new_name) {
        col_name_ = new_name;
        col_id_ = std::move(new_name);
        return *this;
    }

    column_builder& id(std::string new_id) {
        if (col_name_ == new_id) {
            throw std::runtime_error{
                    "The ID is automatically set with name(). If you need to specify them individually, use "
                    "name_only() "
                    "and id_only()."};
        }
        col_id_ = std::move(new_id);
        return *this;
    }

    column_builder& name_only(std::string name) {
        col_name_ = std::move(name);
        return *this;
    }

    column_builder& id_only(std::string id) {
        col_id_ = std::move(id);
        return *this;
    }

    column_builder& cache_key(const std::string& new_cache_key) {
        cache_key_ = new_cache_key;
        return *this;
    }

    column_builder& processing_state(const memory::column_processing_state& state) {
        state_ = state;
        state_modified_ = true;
        return *this;
    }

    column_builder& default_processing_state() {
        state_modified_ = false;
        return processing_state(memory::column_processing_state());
    }

    memory::column_t build() {
        memory::column_t result = memory::builders::temp_column_builder(memory::col_name{col_name_},
                                                                        memory::col_id{col_id_}, owner_, cache_key_)
                                          .create_from_data<T>(static_cast<row_id>(data_size_), std::move(data_array_),
                                                               null_flags_, state_);
        reset();
        return result;
    }

    memory::column_t build_dictified() {
        memory::column_t result{build()};
        std::ignore = result->get_dict(common::execution_context{}); // force dictification
        return result;
    }

    memory::column_t build_and_add_to_owner() {
        if (owner_ == nullptr) {
            throw std::runtime_error{"owner is not set."};
        }
        auto result{owner_->add_column(memory::col_name{col_name_}, memory::col_id{col_id_},
                                       memory::col_cache_key{cache_key_}, std::move(data_array_), null_flags_, state_,
                                       memory::MAX_TABLE_ROW_LIMIT)};
        reset();
        return result;
    }

    memory::column_t build_dictified_and_add_to_owner() {
        memory::column_t result{build_and_add_to_owner()};
        std::ignore = result->get_dict(common::execution_context{}); // force dictification
        return result;
    }

protected:
    void reset() {
        size_set_ = false;
        data_set_ = false;
        owner_ = nullptr;
    }

private:
    bool size_set_ = false;
    bool data_set_ = false;
    bool state_modified_ = false;
    row_id data_size_ = 0;
    legacy_embedded_ctl::static_array<T> data_array_;
    memory::null_flags_t null_flags_;
    memory::column_processing_state state_ = memory::column_processing_state();
    memory::table* owner_ = nullptr;
    std::string col_name_;
    std::string col_id_;
    std::string cache_key_;
};

template <>
class column_builder<cel_string_t> {
public:
    column_builder() = default;

    column_builder& owner(memory::table* new_owner) {
        owner_ = new_owner;
        return *this;
    }

    column_builder& size(int32_t size) { return column_builder::size(static_cast<int64_t>(size)); }

    column_builder& size(int64_t size) {
        if (size < 0) {
            throw std::runtime_error{"Negative size not supported."};
        }
        return column_builder::size(static_cast<size_t>(size));
    }

    column_builder& size(const size_t size) {
        if (size > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
            throw std::runtime_error{"Size is too large."};
        }
        auto row_id_size{static_cast<row_id>(size)};
        if (data_set_) {
            throw std::runtime_error{"Column data is already set."};
        }
        if (size_set_) {
            if (data_size_ != row_id_size) {
                throw std::runtime_error{"Data size does not match stated size."};
            }
        } else {
            data_size_ = row_id_size;
            data_array_ = std::vector<std::string>(size);
            null_flags_ = memory::create_null_flags(size, common::execution_context{});
            size_set_ = true;
        }
        return *this;
    }

    template <typename... TS>
    column_builder& data(TS... input) {
        return data(utils::nullable_vec_t<cel_string_t>{input...});
    }

    column_builder& data(const utils::nullable_vec_t<cel_string_t>& input) {
        size(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            const bool is_null{input[i].is_null()};
            null_flags_->set(i, is_null);
            if (!is_null) {
                data_array_[i] = input[i].get_or_throw();
            }
        }
        data_set_ = true;
        return *this;
    }

    column_builder& data(const std::vector<std::string>& input) {
        size(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            data_array_[i] = input[i];
            null_flags_->set(i, false);
        }
        data_set_ = true;
        return *this;
    }

    /**
   * Generates the column data using a functor. The size needs to be specified in advance. Both the data *and* the
   * null flags can be initialized with this function.
   *
   * @param f A function with a single argument (the row index) which should return a type that is convertible
   *        to a nullable_pql_value<T>.
   */
    template <typename FUNCTOR>
    column_builder& generate_data(FUNCTOR f) {
        if (data_set_) {
            throw std::runtime_error{"Column data is already set."};
        }
        if (!size_set_) {
            throw std::runtime_error{"Size needs to be set for data to be generated."};
        }

        for (row_id i = 0; i < data_size_; ++i) {
            // Not using {}-init to allow narrowing conversion (for now, as it affects a lot of tests)
            utils::nullable_pql_value<cel_string_t> value(f(i));
            null_flags_->set(i, value.is_null());
            if (!value.is_null()) {
                data_array_[i] = value.get_or_throw();
            }
        }
        data_set_ = true;
        return *this;
    }

    column_builder& name(std::string new_name) {
        col_name_ = new_name;
        col_id_ = std::move(new_name);
        return *this;
    }

    column_builder& id(std::string new_id) {
        if (col_name_ == new_id) {
            throw std::runtime_error{
                    "The ID is automatically set with name(). If you need to specify them individually, use "
                    "name_only() "
                    "and id_only()."};
        }
        col_id_ = std::move(new_id);
        return *this;
    }

    column_builder& name_only(std::string name) {
        col_name_ = std::move(name);
        return *this;
    }

    column_builder& id_only(std::string id) {
        col_id_ = std::move(id);
        return *this;
    }

    column_builder& cache_key(const std::string& new_cache_key) {
        cache_key_ = new_cache_key;
        return *this;
    }

    column_builder& processing_state(memory::column_processing_state state) {
        state_ = std::move(state);
        state_modified_ = true;
        return *this;
    }

    column_builder& default_processing_state() {
        state_modified_ = false;
        return processing_state(memory::column_processing_state());
    }

    memory::column_t build() {
        // cannot set the state; default value: data::column_processing_state()
        if (state_modified_) {
            throw std::runtime_error{"State cannot be modified for cel_string_t columns"};
        }

        auto buf_result{create_string_buffer()};

        memory::column_t result{
                memory::builders::temp_column_builder(memory::col_name(col_name_), memory::col_id(col_id_), owner_,
                                                      cache_key_)
                        .create_from_string_data(static_cast<row_id>(data_size_), std::move(buf_result.ptrs),
                                                 static_cast<row_id>(buf_result.string_buf.size()),
                                                 std::move(buf_result.string_buf), null_flags_, state_)};
        reset();
        return result;
    }

    memory::column_t build_dictified() {
        memory::column_t result{build()};
        std::ignore = result->get_dict(common::execution_context{}); // force dictification
        return result;
    }

    memory::column_t build_and_add_to_owner() {
        if (owner_ == nullptr) {
            throw std::runtime_error{"owner is not set."};
        }

        auto buf_result{create_string_buffer()};

        auto result{owner_->add_string_column(memory::col_name(col_name_), memory::col_id(col_id_),
                                              memory::col_cache_key{cache_key_}, std::move(buf_result.ptrs),
                                              std::move(buf_result.string_buf), null_flags_,
                                              memory::MAX_TABLE_ROW_LIMIT)};
        reset();
        return result;
    }

    memory::column_t build_dictified_and_add_to_owner() {
        memory::column_t result{build_and_add_to_owner()};
        std::ignore = result->get_dict(common::execution_context{}); // force dictification
        return result;
    }

protected:
    void reset() {
        size_set_ = false;
        data_set_ = false;
        owner_ = nullptr;
    }

private:
    bool size_set_ = false;
    bool data_set_ = false;
    bool state_modified_ = false;
    row_id data_size_ = 0;
    std::vector<std::string> data_array_;
    memory::null_flags_t null_flags_;
    memory::column_processing_state state_ = memory::column_processing_state();
    memory::table* owner_ = nullptr;
    std::string col_name_;
    std::string col_id_;
    std::string cache_key_;

    struct string_buf_and_ptrs {
        legacy_embedded_ctl::static_array<char> string_buf;
        legacy_embedded_ctl::static_array<cel_string_t> ptrs;
    };

    string_buf_and_ptrs create_string_buffer() {
        common::char_buffer arena;
        auto ptrs{legacy_embedded_ctl::make_static_array_for_overwrite<cel_string_t>(
                legacy_embedded_ctl::cast<size_t>(data_size_),
                LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
        cel_string_t offset{nullptr};
        ska::bytell_hash_map<cel_string_t, cel_string_t> dict;
        for (row_id i = 0; i < data_size_; ++i) {
            const size_t buf_size{data_array_[i].size() + 1};
            char* const copy{arena.request(buf_size)};
            std::copy_n(data_array_[i].c_str(), buf_size, copy);
            auto [it, inserted] = dict.emplace(copy, offset);
            if (inserted) {
                offset += buf_size; // Advance the offset by the size of the consumed buffer.
            } else {
                arena.pop_last(buf_size); // We don't need the copy, since it was already present.
            }
            ptrs[i] =
                    it->second; // This is actually an offset; we correct it below once we have the final base address.
        }
        // Re-allocate to a contiguous buffer and add the base pointer address to the offsets we added earlier.
        auto contiguous = arena.reallocate_to_continuous_buffer_and_reset();
        for (row_id i = 0; i < data_size_; ++i) {
            ptrs[i] += reinterpret_cast<ptrdiff_t>(contiguous.data());
        }
        return string_buf_and_ptrs{std::move(contiguous), std::move(ptrs)};
    }
};
} // namespace celonis::accelerator
