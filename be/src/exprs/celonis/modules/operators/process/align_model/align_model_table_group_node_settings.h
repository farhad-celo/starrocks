#pragma once
#include <chrono>

#include <cpml/conformance/alignment_settings.h>
#include <ctl/named_type.h>

namespace celonis::accelerator::operators::process::align_model {

enum class align_model_version { V1 };

[[nodiscard]] static constexpr std::string_view get_user_visible_operator_name(align_model_version version) noexcept {
  switch (version) {
    case align_model_version::V1:
      return "ALIGN_MODEL";
  }
  return "";
}

class align_model_table_group_node_settings {
 public:
  static constexpr std::chrono::minutes DEFAULT_ALIGN_MODEL_TIMEOUT{10};

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
    [[nodiscard]] align_model_table_group_node_settings build() const& {
      return align_model_table_group_node_settings{align_model_timeout_, alignment_execution_strategy_, version_};
    }
    [[nodiscard]] align_model_table_group_node_settings build() && {
      return align_model_table_group_node_settings{align_model_timeout_, alignment_execution_strategy_, version_};
    }

   private:
    std::chrono::milliseconds align_model_timeout_{DEFAULT_ALIGN_MODEL_TIMEOUT};
    cpml::conformance::alignment_execution_strategy alignment_execution_strategy_{
        cpml::conformance::alignment_execution_strategy::DEFAULT};
    align_model_version version_{align_model_version::V1};
  };

  align_model_table_group_node_settings() = delete;
  static align_model_table_group_node_settings make_default() { return builder{}.build(); }
  [[nodiscard]] std::chrono::milliseconds get_align_model_timeout() const { return align_model_timeout_; }
  [[nodiscard]] cpml::conformance::alignment_execution_strategy get_alignment_execution_strategy() const {
    return alignment_execution_strategy_;
  }
  [[nodiscard]] align_model_version get_version() const { return version_; }

 private:
  align_model_table_group_node_settings(
      std::chrono::milliseconds align_model_timeout,
      const cpml::conformance::alignment_execution_strategy alignment_execution_strategy, align_model_version version)
      : align_model_timeout_{align_model_timeout},
        alignment_execution_strategy_{alignment_execution_strategy},
        version_{version} {}
  std::chrono::milliseconds align_model_timeout_;
  cpml::conformance::alignment_execution_strategy alignment_execution_strategy_;
  align_model_version version_;
};

}  // namespace celonis::accelerator::operators::process::align_model