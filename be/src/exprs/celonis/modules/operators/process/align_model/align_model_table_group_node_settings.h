#pragma once
#include <chrono>

#include <cpml/conformance/alignment_settings.h>
#include <ctl/named_type.h>

namespace celonis::accelerator::operators::process::align_model {

enum class align_model_version { V1, V2, V3 };

[[nodiscard]] static constexpr std::string_view get_user_visible_operator_name(align_model_version version) noexcept {
  switch (version) {
    case align_model_version::V1:
      return "ALIGN_MODEL";
    case align_model_version::V2:
    case align_model_version::V3:
      return "CREATE_ALIGNMENT";
  }
  return "";
}

class align_model_table_group_node_settings {
 public:
  static constexpr std::chrono::minutes DEFAULT_ALIGN_MODEL_TIMEOUT{10};
  static constexpr size_t DEFAULT_CREATE_TABLE_GRAIN_SIZE{100'000};

  using align_model_timeout_t = ctl::named_type<std::chrono::milliseconds, struct align_model_timeout_tag>;
  using metrics_computation_timeout_t =
      ctl::named_type<std::chrono::milliseconds, struct metrics_computation_timeout_tag>;

  class builder {
   public:
    builder() = default;
    builder& set_align_model_timeout(std::chrono::milliseconds val) {
      align_model_timeout_ = val;
      return *this;
    }
    builder& set_alignment_execution_strategy(
        const cpml::conformance::alignment_execution_strategy alignment_execution_strategy) {
      alignment_execution_strategy_ = alignment_execution_strategy;
      return *this;
    }
    builder& set_version(align_model_version val) {
      version_ = val;
      return *this;
    }
    builder& set_create_table_grain_size(size_t val) {
      create_table_grain_size_ = val;
      return *this;
    }
    [[nodiscard]] align_model_table_group_node_settings build() const& {
      return align_model_table_group_node_settings{align_model_timeout_, alignment_execution_strategy_, version_,
                                                   create_table_grain_size_};
    }
    [[nodiscard]] align_model_table_group_node_settings build() && {
      return align_model_table_group_node_settings{align_model_timeout_, alignment_execution_strategy_, version_,
                                                   create_table_grain_size_};
    }

   private:
    std::chrono::milliseconds align_model_timeout_{DEFAULT_ALIGN_MODEL_TIMEOUT};
    cpml::conformance::alignment_execution_strategy alignment_execution_strategy_{
        cpml::conformance::alignment_execution_strategy::DEFAULT};
    align_model_version version_{align_model_version::V1};
    size_t create_table_grain_size_{DEFAULT_CREATE_TABLE_GRAIN_SIZE};
  };

  align_model_table_group_node_settings() = delete;
  static align_model_table_group_node_settings make_default() { return builder{}.build(); }
  [[nodiscard]] std::chrono::milliseconds get_align_model_timeout() const { return align_model_timeout_; }
  [[nodiscard]] cpml::conformance::alignment_execution_strategy get_alignment_execution_strategy() const {
    return alignment_execution_strategy_;
  }
  [[nodiscard]] align_model_version get_version() const { return version_; }
  [[nodiscard]] size_t get_create_table_grain_size() const { return create_table_grain_size_; }

 private:
  align_model_table_group_node_settings(
      std::chrono::milliseconds align_model_timeout,
      const cpml::conformance::alignment_execution_strategy alignment_execution_strategy, align_model_version version,
      size_t create_table_grain_size)
      : align_model_timeout_{align_model_timeout},
        alignment_execution_strategy_{alignment_execution_strategy},
        version_{version},
        create_table_grain_size_{create_table_grain_size} {}
  std::chrono::milliseconds align_model_timeout_;
  cpml::conformance::alignment_execution_strategy alignment_execution_strategy_;
  align_model_version version_;
  size_t create_table_grain_size_;
};

}  // namespace celonis::accelerator::operators::process::align_model