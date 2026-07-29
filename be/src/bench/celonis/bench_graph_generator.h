#pragma once

#include <cstdint>
#include <vector>

namespace starrocks {

struct EdgeListGraph {
    std::vector<int64_t> out_vals;
    std::vector<int64_t> in_vals;
    std::vector<int64_t> pk_vals;
};

class BenchmarkGraphGenerator {
public:
    static std::vector<std::vector<int64_t>> generate_standard_dag_adj_list(int num_nodes) {
        std::vector<std::vector<int64_t>> adj(num_nodes);
        for (int i = 0; i < num_nodes; ++i) {
            if (i + 1 < num_nodes) adj[i].push_back(i + 1);
            if (i + 2 < num_nodes) adj[i].push_back(i + 2);
        }
        return adj;
    }

    static EdgeListGraph generate_standard_dag_edge_list(int num_nodes) {
        EdgeListGraph graph;
        graph.out_vals.reserve(num_nodes * 2);
        graph.in_vals.reserve(num_nodes * 2);
        graph.pk_vals.reserve(num_nodes * 2);

        int64_t pk = 0;
        for (int i = 0; i < num_nodes; ++i) {
            if (i + 1 < num_nodes) {
                graph.out_vals.push_back(i);
                graph.in_vals.push_back(i + 1);
                graph.pk_vals.push_back(pk++);
            }
            if (i + 2 < num_nodes) {
                graph.out_vals.push_back(i);
                graph.in_vals.push_back(i + 2);
                graph.pk_vals.push_back(pk++);
            }
        }
        return graph;
    }
};

} // namespace starrocks