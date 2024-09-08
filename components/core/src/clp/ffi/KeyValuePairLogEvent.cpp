#include "KeyValuePairLogEvent.hpp"

#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json/single_include/nlohmann/json.hpp>
#include <outcome/single-header/outcome.hpp>

#include "../ir/EncodedTextAst.hpp"
#include "../time_types.hpp"
#include "SchemaTree.hpp"
#include "SchemaTreeNode.hpp"
#include "Value.hpp"

using clp::ir::EightByteEncodedTextAst;
using clp::ir::FourByteEncodedTextAst;
using std::string;
using std::vector;

namespace clp::ffi {
namespace {
/**
 * Function to handle a JSON exception.
 * @tparam Func
 */
template <typename Func>
concept JsonExceptionHandlerConcept = std::is_invocable_v<Func, nlohmann::json::exception const&>;

/**
 * Helper class for `KeyValuePairLogEvent::serialize_to_json` used to:
 * - iterate over the children of a non-leaf schema tree node, so long as those children are in the
 *   subtree defined by the `KeyValuePairLogEvent`.
 * - group a non-leaf schema tree node with the JSON object that it's being serialized into.
 * - add the node's corresponding JSON object to its parent's corresponding JSON object (or if the
 *   node is the root, replace the parent JSON object) when this class is destructed.
 * @tparam JsonExceptionHandler Handler for any `nlohmann::json::exception` that occurs during
 * destruction.
 */
template <JsonExceptionHandlerConcept JsonExceptionHandler>
class JsonSerializationIterator {
public:
    // Constructor
    JsonSerializationIterator(
            SchemaTreeNode const* schema_tree_node,
            vector<bool> const& schema_subtree_bitmap,
            nlohmann::json::object_t* parent_json_obj,
            JsonExceptionHandler json_exception_callback
    )
            : m_schema_tree_node{schema_tree_node},
              m_parent{parent_json_obj},
              m_json_exception_callback{json_exception_callback} {
        for (auto const child_id : schema_tree_node->get_children_ids()) {
            if (schema_subtree_bitmap[child_id]) {
                m_children.push_back(child_id);
            }
        }
        m_curr_child_it = m_children.cbegin();
    }

    // Delete copy/move constructor and assignment
    JsonSerializationIterator(JsonSerializationIterator const&) = delete;
    JsonSerializationIterator(JsonSerializationIterator&&) = delete;
    auto operator=(JsonSerializationIterator const&) -> JsonSerializationIterator& = delete;
    auto operator=(JsonSerializationIterator&&) -> JsonSerializationIterator& = delete;

    // Destructor
    ~JsonSerializationIterator() {
        try {
            // If the current node is the root, then replace the `parent` with this node's JSON
            // object. Otherwise, add this node's JSON object as a child of the parent JSON object.
            if (m_schema_tree_node->get_id() == SchemaTree::cRootId) {
                *m_parent = std::move(m_map);
            } else {
                m_parent->emplace(string{m_schema_tree_node->get_key_name()}, std::move(m_map));
            }
        } catch (nlohmann::json::exception const& ex) {
            m_json_exception_callback(ex);
        }
    }

    /**
     * @return whether there are more children to traverse.
     */
    [[nodiscard]] auto has_next_child() const -> bool {
        return m_curr_child_it != m_children.end();
    }

    /**
     * Gets the next child and advances the underlying child idx.
     * @return the next child to traverse.
     */
    [[nodiscard]] auto get_next_child() -> SchemaTreeNode::id_t { return *(m_curr_child_it++); }

    [[nodiscard]] auto get_map() -> nlohmann::json::object_t& { return m_map; }

private:
    SchemaTreeNode const* m_schema_tree_node;
    vector<SchemaTreeNode::id_t> m_children;
    vector<SchemaTreeNode::id_t>::const_iterator m_curr_child_it;
    nlohmann::json::object_t* m_parent;
    nlohmann::json::object_t m_map;
    JsonExceptionHandler m_json_exception_callback;
};

/**
 * @param type
 * @param value
 * @return Whether the given schema tree node type matches the given value's type.
 */
[[nodiscard]] auto
node_type_matches_value_type(SchemaTreeNode::Type type, Value const& value) -> bool;

/**
 * Validates whether the given node-ID value pairs are leaf nodes in the `SchemaTree` forming a
 * sub-tree of their own.
 * @param schema_tree
 * @param node_id_value_pairs
 * @return success if the inputs are valid, or an error code indicating the failure:
 * - std::errc::operation_not_permitted if a node ID doesn't represent a valid node in the
 *   schema tree, or a non-leaf node ID is paired with a value.
 * - std::errc::protocol_error if the schema tree node type doesn't match the value's type.
 * - std::errc::protocol_not_supported if the same key appears more than once under a parent
 *   node.
 */
[[nodiscard]] auto validate_node_id_value_pairs(
        SchemaTree const& schema_tree,
        KeyValuePairLogEvent::NodeIdValuePairs const& node_id_value_pairs
) -> std::errc;

/**
 * @param schema_tree
 * @param node_id
 * @param node_id_value_pairs
 * @return Whether the given node is a leaf node in the sub-tree of the `SchemaTree` defined by
 * `node_id_value_pairs`. A node is considered a leaf if none of its descendants appear in
 * `node_id_value_pairs`.
 */
[[nodiscard]] auto is_leaf_node(
        SchemaTree const& schema_tree,
        SchemaTreeNode::id_t node_id,
        KeyValuePairLogEvent::NodeIdValuePairs const& node_id_value_pairs
) -> bool;

/**
 * @param node_id_value_pairs
 * @param schema_tree
 * @return A result containing a bitmap where every bit corresponds to the ID of a node in the
 * schema tree, and the set bits correspond to the nodes in the subtree defined by all paths from
 * the root node to the nodes in `node_id_value_pairs`; or an error code indicating a failure:
 * - std::errc::result_out_of_range if a node ID in `node_id_value_pairs` doesn't exist in the
 *   schema tree.
 */
[[nodiscard]] auto get_schema_subtree_bitmap(
        KeyValuePairLogEvent::NodeIdValuePairs const& node_id_value_pairs,
        SchemaTree const& schema_tree
) -> OUTCOME_V2_NAMESPACE::std_result<vector<bool>>;

/**
 * Inserts the given key-value pair into the JSON object (map).
 * @param node The schema tree node of the key to insert.
 * @param optional_val The value to insert.
 * @param json_obj Outputs the inserted JSON object.
 * @return Whether the insertion was successful.
 */
[[nodiscard]] auto insert_kv_pair_into_json_obj(
        SchemaTreeNode const& node,
        std::optional<Value> const& optional_val,
        nlohmann::json::object_t& json_obj
) -> bool;

/**
 * Decodes a value as an `EncodedTextAst` according to the encoding type.
 * NOTE: this method assumes the upper level caller already checked that `val` is either
 * `FourByteEncodedTextAst` or `EightByteEncodedTextAst`.
 * @param val
 * @return Same as `EncodedTextAst::decode_and_unparse`.
 */
[[nodiscard]] auto decode_as_encoded_text_ast(Value const& val) -> std::optional<string>;

auto node_type_matches_value_type(SchemaTreeNode::Type type, Value const& value) -> bool {
    switch (type) {
        case SchemaTreeNode::Type::Obj:
            return value.is_null();
        case SchemaTreeNode::Type::Int:
            return value.is<value_int_t>();
        case SchemaTreeNode::Type::Float:
            return value.is<value_float_t>();
        case SchemaTreeNode::Type::Bool:
            return value.is<value_bool_t>();
        case SchemaTreeNode::Type::UnstructuredArray:
            return value.is<FourByteEncodedTextAst>() || value.is<EightByteEncodedTextAst>();
        case SchemaTreeNode::Type::Str:
            return value.is<string>() || value.is<FourByteEncodedTextAst>()
                   || value.is<EightByteEncodedTextAst>();
        default:
            return false;
    }
}

auto validate_node_id_value_pairs(
        SchemaTree const& schema_tree,
        KeyValuePairLogEvent::NodeIdValuePairs const& node_id_value_pairs
) -> std::errc {
    try {
        std::unordered_map<SchemaTreeNode::id_t, std::unordered_set<std::string_view>>
                parent_node_id_to_key_names;
        for (auto const& [node_id, value] : node_id_value_pairs) {
            if (SchemaTree::cRootId == node_id) {
                return std::errc::operation_not_permitted;
            }

            auto const& node{schema_tree.get_node(node_id)};
            auto const node_type{node.get_type()};
            if (false == value.has_value()) {
                // Value is an empty object (`{}`, which is not the same as `null`)
                if (SchemaTreeNode::Type::Obj != node_type) {
                    return std::errc::protocol_error;
                }
            } else if (false == node_type_matches_value_type(node_type, value.value())) {
                return std::errc::protocol_error;
            }

            if (SchemaTreeNode::Type::Obj == node_type
                && false == is_leaf_node(schema_tree, node_id, node_id_value_pairs))
            {
                // The node's value is `null` or `{}` but its descendants appear in
                // `node_id_value_pairs`.
                return std::errc::operation_not_permitted;
            }

            auto const parent_node_id{node.get_parent_id()};
            auto const key_name{node.get_key_name()};
            if (parent_node_id_to_key_names.contains(parent_node_id)) {
                auto const [it, new_key_inserted]{
                        parent_node_id_to_key_names.at(parent_node_id).emplace(key_name)
                };
                if (false == new_key_inserted) {
                    // The key is duplicated under the same parent
                    return std::errc::protocol_not_supported;
                }
            } else {
                parent_node_id_to_key_names.emplace(parent_node_id, std::unordered_set{key_name});
            }
        }
    } catch (SchemaTree::OperationFailed const& ex) {
        return std::errc::operation_not_permitted;
    }
    return std::errc{};
}

auto is_leaf_node(
        SchemaTree const& schema_tree,
        SchemaTreeNode::id_t node_id,
        KeyValuePairLogEvent::NodeIdValuePairs const& node_id_value_pairs
) -> bool {
    vector<SchemaTreeNode::id_t> dfs_stack;
    dfs_stack.reserve(schema_tree.get_size());
    dfs_stack.push_back(node_id);
    while (false == dfs_stack.empty()) {
        auto const curr_node_id{dfs_stack.back()};
        dfs_stack.pop_back();
        for (auto const child_node_id : schema_tree.get_node(curr_node_id).get_children_ids()) {
            if (node_id_value_pairs.contains(child_node_id)) {
                return false;
            }
            dfs_stack.push_back(child_node_id);
        }
    }
    return true;
}

auto get_schema_subtree_bitmap(
        KeyValuePairLogEvent::NodeIdValuePairs const& node_id_value_pairs,
        SchemaTree const& schema_tree
) -> OUTCOME_V2_NAMESPACE::std_result<vector<bool>> {
    auto schema_subtree_bitmap{vector<bool>(schema_tree.get_size(), false)};
    for (auto const& [node_id, val] : node_id_value_pairs) {
        if (node_id >= schema_subtree_bitmap.size()) {
            return std::errc::result_out_of_range;
        }
        schema_subtree_bitmap[node_id] = true;

        // Iteratively mark the parents as true
        auto parent_id{schema_tree.get_node(node_id).get_parent_id()};
        while (true) {
            if (schema_subtree_bitmap[parent_id]) {
                // Parent already set by other child
                break;
            }
            schema_subtree_bitmap[parent_id] = true;
            if (SchemaTree::cRootId == parent_id) {
                break;
            }
            parent_id = schema_tree.get_node(parent_id).get_parent_id();
        }
    }
    return schema_subtree_bitmap;
}

auto insert_kv_pair_into_json_obj(
        SchemaTreeNode const& node,
        std::optional<Value> const& optional_val,
        nlohmann::json::object_t& json_obj
) -> bool {
    auto const key_name{node.get_key_name()};
    auto const type{node.get_type()};
    if (false == optional_val.has_value()) {
        json_obj.emplace(string{key_name}, nlohmann::json::object());
        return true;
    }

    string const key_name_str{key_name};
    try {
        auto const& val{optional_val.value()};
        switch (type) {
            case SchemaTreeNode::Type::Int:
                json_obj.emplace(key_name_str, val.get_immutable_view<value_int_t>());
                break;
            case SchemaTreeNode::Type::Float:
                json_obj.emplace(key_name_str, val.get_immutable_view<value_float_t>());
                break;
            case SchemaTreeNode::Type::Bool:
                json_obj.emplace(key_name_str, val.get_immutable_view<bool>());
                break;
            case SchemaTreeNode::Type::Str:
                if (val.is<string>()) {
                    json_obj.emplace(key_name_str, string{val.get_immutable_view<string>()});
                } else {
                    auto const decoded_result{decode_as_encoded_text_ast(val)};
                    if (false == decoded_result.has_value()) {
                        return false;
                    }
                    json_obj.emplace(key_name_str, decoded_result.value());
                }
                break;
            case SchemaTreeNode::Type::UnstructuredArray: {
                auto const decoded_result{decode_as_encoded_text_ast(val)};
                if (false == decoded_result.has_value()) {
                    return false;
                }
                json_obj.emplace(key_name_str, nlohmann::json::parse(decoded_result.value()));
                break;
            }
            case SchemaTreeNode::Type::Obj:
                json_obj.emplace(key_name_str, nullptr);
                break;
            default:
                return false;
        }
    } catch (nlohmann::json::exception const& ex) {
        return false;
    } catch (Value::OperationFailed const& ex) {
        return false;
    }

    return true;
}

auto decode_as_encoded_text_ast(Value const& val) -> std::optional<string> {
    return val.is<FourByteEncodedTextAst>()
                   ? val.get_immutable_view<FourByteEncodedTextAst>().decode_and_unparse()
                   : val.get_immutable_view<EightByteEncodedTextAst>().decode_and_unparse();
}
}  // namespace

auto KeyValuePairLogEvent::create(
        std::shared_ptr<SchemaTree const> schema_tree,
        NodeIdValuePairs node_id_value_pairs,
        UtcOffset utc_offset
) -> OUTCOME_V2_NAMESPACE::std_result<KeyValuePairLogEvent> {
    if (auto const ret_val{validate_node_id_value_pairs(*schema_tree, node_id_value_pairs)};
        std::errc{} != ret_val)
    {
        return ret_val;
    }
    return KeyValuePairLogEvent{std::move(schema_tree), std::move(node_id_value_pairs), utc_offset};
}

auto KeyValuePairLogEvent::serialize_to_json(
) const -> OUTCOME_V2_NAMESPACE::std_result<nlohmann::json> {
    if (m_node_id_value_pairs.empty()) {
        return nlohmann::json::object();
    }

    bool json_exception_captured{false};
    auto json_exception_handler = [&]([[maybe_unused]] nlohmann::json::exception const& ex
                                  ) -> void { json_exception_captured = true; };
    using DfsIterator = JsonSerializationIterator<decltype(json_exception_handler)>;

    // NOTE: We use a `std::stack` (which uses `std::deque` as the underlying container) instead of
    // a `std::vector` to avoid implementing move semantics for `DfsIterator` (required when the
    // vector grows).
    std::stack<DfsIterator> dfs_stack;

    auto const schema_subtree_bitmap_ret{
            get_schema_subtree_bitmap(m_node_id_value_pairs, *m_schema_tree)
    };
    if (schema_subtree_bitmap_ret.has_error()) {
        return schema_subtree_bitmap_ret.error();
    }
    auto const& schema_subtree_bitmap{schema_subtree_bitmap_ret.value()};

    // Traverse the schema tree in DFS order, but only traverse the nodes that are set in
    // `schema_subtree_bitmap`.
    //
    // On the way down:
    // - for each non-leaf node, create a `nlohmann::json::object_t`;
    // - for each leaf node, insert the key-value pair into the parent `nlohmann::json::object_t`.
    //
    // On the way up, add the current node's `nlohmann::json::object_t` to the parent's
    // `nlohmann::json::object_t`.
    auto const& root_node{m_schema_tree->get_node(SchemaTree::cRootId)};
    auto json_root = nlohmann::json::object_t();

    dfs_stack.emplace(&root_node, schema_subtree_bitmap, &json_root, json_exception_handler);
    while (false == dfs_stack.empty() && false == json_exception_captured) {
        auto& top{dfs_stack.top()};
        if (false == top.has_next_child()) {
            dfs_stack.pop();
            continue;
        }
        auto const child_node_id{top.get_next_child()};
        auto const& child_node{m_schema_tree->get_node(child_node_id)};
        if (m_node_id_value_pairs.contains(child_node_id)) {
            // Handle leaf node
            if (false
                == insert_kv_pair_into_json_obj(
                        child_node,
                        m_node_id_value_pairs.at(child_node_id),
                        top.get_map()
                ))
            {
                return std::errc::protocol_error;
            }
        } else {
            dfs_stack.emplace(
                    &child_node,
                    schema_subtree_bitmap,
                    &top.get_map(),
                    json_exception_handler
            );
        }
    }

    if (json_exception_captured) {
        return std::errc::protocol_error;
    }

    return json_root;
}
}  // namespace clp::ffi
