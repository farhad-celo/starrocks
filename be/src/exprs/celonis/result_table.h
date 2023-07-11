#pragma once

#include <map>
#include <memory>
#include <string>

namespace starrocks {
namespace celonis {

class ResultColumnBase {
public:
    ResultColumnBase(const std::string& name, size_t size) : name_(name), size_(size) {}
    virtual ~ResultColumnBase() = default;

    const std::string& name() { return name_; }
    size_t size() const { return size_; }

private:
    const std::string name_;
    const size_t size_;
};

template <typename TYPE>
class ResultColumn : public ResultColumnBase {
public:
    ResultColumn(const std::string& name, size_t size) : ResultColumnBase(name, size) {
        buffer_ = std::make_unique<TYPE[]>(size);
    }

    TYPE& operator[](int index) { return buffer_[index]; }

    const TYPE& operator[](int index) const { return buffer_[index]; }

private:
    std::unique_ptr<TYPE[]> buffer_;
};

template <typename TYPE>
class NullableResultColumn : public ResultColumn<TYPE> {
public:
    NullableResultColumn(const std::string& name, size_t size) : ResultColumn<TYPE>(name, size) {
        null_ = std::make_unique<bool[]>(size);
    }

    void set_null(int index) { null_[index] = true; }
    bool is_null(int index) const { return null_[index]; }

private:
    std::unique_ptr<bool[]> null_;
};

class ResultTable {
public:
    ResultTable(const std::string& name, size_t size) : name_(name), size_(size) {}

    template <typename TYPE>
    TYPE* AddColumn(const std::string& column_name) {
        auto column = new TYPE(column_name, size_);
        columns_.emplace(std::make_pair(column_name, column));
        return column;
    }

    const ResultColumnBase* column(const std::string& column_name) const {
        auto it = columns_.find(column_name);
        if (it == columns_.end()) return nullptr;
        return it->second.get();
    };

    const std::map<std::string, std::unique_ptr<ResultColumnBase>>& columns() const { return columns_; };
    const std::string& name() const { return name_; }
    size_t size() const { return size_; }

private:
    std::map<std::string, std::unique_ptr<ResultColumnBase>> columns_;
    const std::string name_;
    size_t size_;
};

} // namespace celonis
} // namespace starrocks
