#pragma once

#include <string_view>

/**
 * Not in namespace 'utils' for more convenient usage of the messages
 */
namespace celonis::accelerator::legacy_embedded_ctl {

// All allocation categories and their reasons are specified here
static constexpr std::string_view RAW_DATA_ALLOC_MSG{"Raw data for the memory manager"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view MEMORY_FOR_DISK_IO_MSG{
    "Load data from disk to main memory"};                                                   // NOLINT(cert-err58-cpp)
static constexpr std::string_view MEMORY_FOR_DECOMPRESSION_MSG{"Memory for decompression"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view RETURN_VALUE_MSG{"Return value allocation"};               // NOLINT(cert-err58-cpp)
static constexpr std::string_view OUTPUT_COLUMN_MSG{"Allocation for the output column"};     // NOLINT(cert-err58-cpp)
static constexpr std::string_view TEMPORARY_COLUMN_MSG{
    "Allocation for the temporary column"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view TEMPORARY_STORAGE_MSG{
    "Allocation for temporary memory storage"};                                       // NOLINT(cert-err58-cpp)
static constexpr std::string_view GROUPER_ALLOCATION_MSG{"Allocation for groupers"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view PERMUTATION_VECTOR_MSG{
    "Allocation for permutation vector"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view CACHED_COLUMN_PTRS_MSG(
    "Allocation for cached column pointers");  // NOLINT(cert-err58-cpp)
static constexpr std::string_view MEMBER_INIT_MSG{
    "Allocation for member variable initialization"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view EMPTY_PLACEHOLDER_ALLOCATION_MSG{
    "Empty placeholder allocation"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view AUGMENTATION_COLUMN_MSG{
    "Allocation of an augmentation column"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view CLUSTER_STRINGS_ALLOC_MSG{
    "Allocation for cluster_strings"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view CLUSTER_STRINGS_DBSCAN_ALLOC_MSG{
    "Allocation for cluster_strings dbscan"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view LINK_OBJECTS_COUNT_ALLOC_MSG{
    "Allocation for LINK_OBJECTS degree counts"};  // NOLINT(cert-err58-cpp)

// Allocation reasons for the memory checked containers in process/pattern
static constexpr std::string_view REGEX_TREE_ACTIVITY_COMPRESSION_ACTIVITY_TO_OP_NODE_MSG{
    "Allocation for regex tree activity compression activity to regex op node mapping"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view REGEX_TREE_ACTIVITY_COMPRESSION_NODE_SET_TO_NODE_SET_ID_MSG{
    "Allocation for regex tree activity compression node set to node set id"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view REGEX_TREE_PREPROCESSING_LEAF_NODES_MSG{
    "Allocation for regex tree preprocessing leaf nodes"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view REGEX_TREE_CHILDREN_MSG{
    "Allocation for regex tree children"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view REGEX_TO_NFA_TRANSITIONS_MSG{
    "Allocation for regex to nfa transitions"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view REGEX_TO_NFA_TRANSITIONS_TRIGGER_TO_TARGETS_MAP_MSG{
    "Allocation for regex to nfa transitions trigger to targets"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view REGEX_TO_NFA_TRANSITIONS_TARGETS_SET_MSG{
    "Allocation for regex to nfa transitions targets set"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view NFA_TO_DFA_DFA_STATE_MSG{
    "Allocation for nfa to dfa dfa state"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view NFA_TO_DFA_DFA_STATE_SET_MSG{
    "Allocation for nfa to dfa dfa state set"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view NFA_TO_DFA_TRANSITION_FUNCTION_MSG{
    "Allocation for nfa to dfa transition function"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view NFA_TO_DFA_ACTIVITY_TO_DFA_TARGET_STATE_MAP_MSG{
    "Allocation for nfa to dfa activity id to dfa target state map"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view NFA_TO_DFA_ALL_DFA_STATES_MSG{
    "Allocation for nfa to dfa all dfa states"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view EXECUTE_LIKE_ON_ARRAY_MSG{
    "Allocation for execute like on array"};  // NOLINT(cert-err58-cpp)

// Allocation reasons for the memory checked containers in the alignment
static constexpr std::string_view ENABLED_TRANSITIONS_CACHE{
    "Allocation for Petri net's enabled transitions cache"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view PATHS_TO_TRANSITION_CACHE{
    "Allocation for Petri net's marking to transition path cache"};  // NOLINT(cert-err58-cpp)
static constexpr std::string_view PATHS_TO_MARKING_CACHE{
    "Allocation for Petri net's marking to marking path cache"};  // NOLINT(cert-err58-cpp)

}  // namespace celonis::accelerator::legacy_embedded_ctl
