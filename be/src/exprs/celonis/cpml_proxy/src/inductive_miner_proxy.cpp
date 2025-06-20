#include "cpml_proxy/inductive_miner_proxy.h"

#include <cpml/discovery/inductive_miner.h>
#include <cpml/discovery/inductive_miner_settings.h>
#include <cpml/model/pt/replay.h>

#include "process_tree_to_table.h"
#include "sr_context.h"
#include "sr_variant_accessor.h"

namespace cpml_proxy {

namespace {

[[nodiscard]] cpml::discovery::inductive_miner_exec_settings make_im_settings(const double imfd_frequency_threshold) {
    using im_settings_builder_t = cpml::discovery::inductive_miner_exec_settings::builder;
    using infrequent_im_settings_builder_t = cpml::discovery::infrequent_im_settings::builder;
    return im_settings_builder_t{}                     //
            .im_policy_settings(                       //
                    infrequent_im_settings_builder_t{} //
                            .edges_filter_frequency_threshold(imfd_frequency_threshold)
                            .build()) // throws if the threshold was not in the valid range
            .build();
}

} // anonymous namespace

inductive_miner_result inductive_miner(const starrocks::Variants& variants, double imfd_frequency_threshold) {
    // Setup IM input
    const sr_variant_accessor variant_accessor{variants};
    const cpml::context::function_context function_ctx{make_sr_function_context()};
    const auto settings{make_im_settings(imfd_frequency_threshold)};

    // Call IM in the CPML
    const auto [process_tree, statistics]{cpml::discovery::inductive_miner(variant_accessor, function_ctx, settings)};

    // Transform IM results to SR output
    auto [vertex_table, edge_table]{convert_pt_to_tables(process_tree)};
    // N.B: statistics are copied
    return {std::move(vertex_table), std::move(edge_table), statistics.data()};
}

} // namespace cpml_proxy
