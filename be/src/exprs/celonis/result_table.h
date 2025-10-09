#pragma once

#include <fmt/format.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace starrocks {
namespace celonis {

class ResultColumnBase {
public:
    ResultColumnBase(std::string_view name, size_t size) : name_(name), size_(size) {}
    virtual ~ResultColumnBase() = default;

    const std::string& name() const { return name_; }
    size_t size() const { return size_; }

private:
    const std::string name_;
    const size_t size_;
};

template <typename TYPE>
class ResultColumn : public ResultColumnBase {
public:
    ResultColumn(std::string_view name, size_t size) : ResultColumnBase(name, size) {
        buffer_ = std::make_unique<TYPE[]>(size);
    }

    TYPE& operator[](int index) { return buffer_[index]; }

    const TYPE& operator[](int index) const { return buffer_[index]; }

    [[nodiscard]] const TYPE& at(int index) const {
        if (index >= size()) {
            throw std::out_of_range(
                    fmt::format("Attempting to access ResultColumn of size [{}] at index [{}].", size(), index));
        }
        return operator[](index);
    };

    [[nodiscard]] TYPE& at(int index) {
        // We implement the non const at in terms of the const at to avoid duplication
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        return const_cast<TYPE&>(static_cast<const ResultColumn&>(*this).at(index));
    };

private:
    std::unique_ptr<TYPE[]> buffer_;
};

template <typename TYPE>
class NullableResultColumn : public ResultColumn<TYPE> {
public:
    NullableResultColumn(std::string_view name, size_t size) : ResultColumn<TYPE>(name, size) {
        null_ = std::make_unique<bool[]>(size);
    }

    void set_null(int index) { null_[index] = true; }
    bool is_null(int index) const { return null_[index]; }

private:
    std::unique_ptr<bool[]> null_;
};

class ResultTable {
public:
    ResultTable(std::string_view name, size_t size) : name_(name), size_(size) {}

    template <typename TYPE>
    ResultColumn<TYPE>& AddColumn(std::string_view column_name) {
        auto column = new ResultColumn<TYPE>(column_name, size_);
        columns_.emplace(std::make_pair(column_name, column));
        return *column;
    }

    template <typename TYPE>
    NullableResultColumn<TYPE>& AddNullableColumn(std::string_view column_name) {
        auto column = new NullableResultColumn<TYPE>(column_name, size_);
        columns_.emplace(std::make_pair(column_name, column));
        return *column;
    }

    template <typename TYPE>
    ResultColumn<TYPE>& column(std::string_view column_name) const {
        return *dynamic_cast<ResultColumn<TYPE>*>(column(std::string(column_name)));
    };

    template <typename TYPE>
    NullableResultColumn<TYPE>& nullable_column(std::string_view column_name) const {
        return *dynamic_cast<NullableResultColumn<TYPE>*>(column(std::string(column_name)));
    };

    const std::map<std::string, std::unique_ptr<ResultColumnBase>>& columns() const { return columns_; };
    const std::string& name() const { return name_; }
    size_t size() const { return size_; }

private:
    ResultColumnBase* column(const std::string& column_name) const {
        auto it = columns_.find(column_name);
        if (it == columns_.end()) return nullptr;
        return it->second.get();
    };

    std::map<std::string, std::unique_ptr<ResultColumnBase>> columns_;
    const std::string name_;
    size_t size_;
};

using ResultTableMap = std::map<std::string, std::unique_ptr<starrocks::celonis::ResultTable>>;

} // namespace celonis
} // namespace starrocks
