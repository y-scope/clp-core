#include "SchemaReader.hpp"

#include <stack>

#include "Schema.hpp"

namespace clp_s {
void SchemaReader::append_column(BaseColumnReader* column_reader) {
    m_column_map[column_reader->get_id()] = column_reader;
    m_columns.push_back(column_reader);
    // The local schema tree is only necessary for generating the JSON template to marshal records.
    if (m_should_marshal_records) {
        generate_local_tree(column_reader->get_id());
    }
}

void SchemaReader::append_unordered_column(BaseColumnReader* column_reader) {
    m_columns.push_back(column_reader);
    if (m_should_marshal_records) {
        generate_local_tree(column_reader->get_id());
    }
}

void SchemaReader::mark_column_as_timestamp(BaseColumnReader* column_reader) {
    m_timestamp_column = column_reader;
    if (m_timestamp_column->get_type() == NodeType::DateString) {
        m_get_timestamp = [this]() {
            return static_cast<DateStringColumnReader*>(m_timestamp_column)
                    ->get_encoded_time(m_cur_message);
        };
    } else if (m_timestamp_column->get_type() == NodeType::Integer) {
        m_get_timestamp = [this]() {
            return std::get<int64_t>(static_cast<Int64ColumnReader*>(m_timestamp_column)
                                             ->extract_value(m_cur_message));
        };
    } else if (m_timestamp_column->get_type() == NodeType::Float) {
        m_get_timestamp = [this]() {
            return static_cast<epochtime_t>(
                    std::get<double>(static_cast<FloatColumnReader*>(m_timestamp_column)
                                             ->extract_value(m_cur_message))
            );
        };
    }
}

void SchemaReader::append_column(int32_t id) {
    // The local schema tree is only necessary for generating the JSON template to marshal records.
    if (m_should_marshal_records) {
        generate_local_tree(id);
    }
}

void SchemaReader::load(ZstdDecompressor& decompressor) {
    for (auto& reader : m_columns) {
        reader->load(decompressor, m_num_messages);
    }

    if (m_should_marshal_records) {
        generate_json_template(0);
    }
}

void SchemaReader::generate_json_string() {
    m_json_serializer.reset();
    m_json_serializer.begin_document();
    size_t column_id_index = 0;
    BaseColumnReader* column;
    JsonSerializer::Op op;
    while (m_json_serializer.get_next_op(op)) {
        switch (op) {
            case JsonSerializer::Op::BeginObject: {
                m_json_serializer.begin_object();
                break;
            }
            case JsonSerializer::Op::EndObject: {
                m_json_serializer.end_object();
                break;
            }
            case JsonSerializer::Op::BeginDocument: {
                m_json_serializer.begin_document();
                break;
            }
            case JsonSerializer::Op::BeginArray: {
                m_json_serializer.begin_array();
                break;
            }
            case JsonSerializer::Op::EndArray: {
                m_json_serializer.end_array();
                break;
            }
            case JsonSerializer::Op::BeginArrayDocument: {
                m_json_serializer.begin_array_document();
                break;
            }
            case JsonSerializer::Op::AddIntField: {
                column = m_reordered_columns[column_id_index++];
                auto const& name = column->get_name();
                if (false == name.empty()) {
                    m_json_serializer.append_key(column->get_name());
                }
                m_json_serializer.append_value(
                        std::to_string(std::get<int64_t>(column->extract_value(m_cur_message)))
                );
                break;
            }
            case JsonSerializer::Op::AddFloatField: {
                column = m_reordered_columns[column_id_index++];
                auto const& name = column->get_name();
                if (false == name.empty()) {
                    m_json_serializer.append_key(column->get_name());
                }
                m_json_serializer.append_value(
                        std::to_string(std::get<double>(column->extract_value(m_cur_message)))
                );
                break;
            }
            case JsonSerializer::Op::AddBoolField: {
                column = m_reordered_columns[column_id_index++];
                auto const& name = column->get_name();
                if (false == name.empty()) {
                    m_json_serializer.append_key(column->get_name());
                }
                m_json_serializer.append_value(
                        std::get<uint8_t>(column->extract_value(m_cur_message)) != 0 ? "true"
                                                                                     : "false"
                );
                break;
            }
            case JsonSerializer::Op::AddStringField: {
                column = m_reordered_columns[column_id_index++];
                auto const& name = column->get_name();
                if (false == name.empty()) {
                    m_json_serializer.append_key(column->get_name());
                }
                m_json_serializer.append_value_with_quotes(
                        std::get<std::string>(column->extract_value(m_cur_message))
                );
                break;
            }
            case JsonSerializer::Op::AddArrayField: {
                column = m_reordered_columns[column_id_index++];
                m_json_serializer.append_key(column->get_name());
                m_json_serializer.append_value(
                        std::get<std::string>(column->extract_value(m_cur_message))
                );
                break;
            }
            case JsonSerializer::Op::AddNullField: {
                m_json_serializer.append_key();
                m_json_serializer.append_value("null");
                break;
            }
            case JsonSerializer::Op::AddNullValue: {
                m_json_serializer.append_value("null");
                break;
            }
        }
    }

    m_json_serializer.end_document();
}

bool SchemaReader::get_next_message(std::string& message) {
    if (m_cur_message >= m_num_messages) {
        return false;
    }

    generate_json_string();

    message = m_json_serializer.get_serialized_string();

    if (message.back() != '\n') {
        message += '\n';
    }

    m_cur_message++;
    return true;
}

bool SchemaReader::get_next_message(std::string& message, FilterClass* filter) {
    while (m_cur_message < m_num_messages) {
        if (false == filter->filter(m_cur_message)) {
            m_cur_message++;
            continue;
        }

        if (m_should_marshal_records) {
            generate_json_string();
            message = m_json_serializer.get_serialized_string();

            if (message.back() != '\n') {
                message += '\n';
            }
        }

        m_cur_message++;
        return true;
    }

    return false;
}

bool SchemaReader::get_next_message_with_timestamp(
        std::string& message,
        epochtime_t& timestamp,
        FilterClass* filter
) {
    // TODO: If we already get max_num_results messages, we can skip messages
    // with the timestamp less than the smallest timestamp in the priority queue
    while (m_cur_message < m_num_messages) {
        if (false == filter->filter(m_cur_message)) {
            m_cur_message++;
            continue;
        }

        if (m_should_marshal_records) {
            generate_json_string();
            message = m_json_serializer.get_serialized_string();

            if (message.back() != '\n') {
                message += '\n';
            }
        }

        timestamp = m_get_timestamp();

        m_cur_message++;
        return true;
    }

    return false;
}

void SchemaReader::initialize_filter(FilterClass* filter) {
    filter->init(this, m_schema_id, m_columns);
}

void SchemaReader::generate_local_tree(int32_t global_id) {
    auto it = m_global_id_to_local_id.find(global_id);
    if (m_global_id_to_local_id.end() != it) {
        return;
    }
    std::stack<int32_t> global_id_stack;
    global_id_stack.emplace(global_id);
    do {
        auto node = m_global_schema_tree->get_node(global_id_stack.top());
        int32_t parent_id = node->get_parent_id();

        auto it = m_global_id_to_local_id.find(parent_id);
        if (-1 != parent_id && it == m_global_id_to_local_id.end()) {
            global_id_stack.emplace(parent_id);
            continue;
        }

        int32_t local_id = m_local_schema_tree->add_node(
                parent_id == -1 ? -1 : m_global_id_to_local_id[parent_id],
                node->get_type(),
                node->get_key_name()
        );

        m_global_id_to_local_id[global_id_stack.top()] = local_id;
        m_local_id_to_global_id[local_id] = global_id_stack.top();
        global_id_stack.pop();
    } while (false == global_id_stack.empty());
}

void SchemaReader::mark_unordered_object(
        size_t column_reader_start,
        int32_t mst_subtree_root,
        Span<int32_t> schema
) {
    m_global_id_to_unordered_object.emplace(
            mst_subtree_root,
            std::make_pair(column_reader_start, schema)
    );
}

int32_t SchemaReader::get_first_column_in_span(Span<int32_t> schema) {
    for (int32_t column_id : schema) {
        if (false == Schema::schema_entry_is_unordered_object(column_id)) {
            return column_id;
        }
    }
    return -1;
}

void SchemaReader::find_intersection_and_fix_brackets(
        int32_t cur_root,
        int32_t next_root,
        std::vector<int32_t>& path_to_intersection
) {
    auto cur_node = m_global_schema_tree->get_node(cur_root);
    auto next_node = m_global_schema_tree->get_node(next_root);
    while (cur_node->get_parent_id() != next_node->get_parent_id()) {
        if (cur_node->get_depth() > next_node->get_depth()) {
            cur_root = cur_node->get_parent_id();
            cur_node = m_global_schema_tree->get_node(cur_root);
            m_json_serializer.add_op(JsonSerializer::Op::EndObject);
        } else if (cur_node->get_depth() < next_node->get_depth()) {
            path_to_intersection.push_back(next_root);
            next_root = next_node->get_parent_id();
            next_node = m_global_schema_tree->get_node(next_root);
        } else {
            cur_root = cur_node->get_parent_id();
            cur_node = m_global_schema_tree->get_node(cur_root);
            m_json_serializer.add_op(JsonSerializer::Op::EndObject);
            path_to_intersection.push_back(next_root);
            next_root = next_node->get_parent_id();
            next_node = m_global_schema_tree->get_node(next_root);
        }
    }

    for (auto it = path_to_intersection.rbegin(); it != path_to_intersection.rend(); ++it) {
        auto node = m_global_schema_tree->get_node(*it);
        bool no_name = true;
        if (false == node->get_key_name().empty()) {
            m_json_serializer.add_special_key(node->get_key_name());
            no_name = false;
        }
        if (NodeType::Object == node->get_type()) {
            m_json_serializer.add_op(
                    no_name ? JsonSerializer::Op::BeginDocument : JsonSerializer::Op::BeginObject
            );
        } else if (NodeType::StructuredArray == node->get_type()) {
            m_json_serializer.add_op(
                    no_name ? JsonSerializer::Op::BeginArrayDocument
                            : JsonSerializer::Op::BeginArray
            );
        }
    }
    path_to_intersection.clear();
}

size_t SchemaReader::generate_structured_array_template(
        int32_t array_root,
        size_t column_start,
        Span<int32_t> schema
) {
    size_t column_idx = column_start;
    std::vector<int32_t> path_to_intersection;
    int32_t depth = m_global_schema_tree->get_node(array_root)->get_depth();

    for (size_t i = 0; i < schema.size(); ++i) {
        int32_t global_column_id = schema[i];
        if (Schema::schema_entry_is_unordered_object(global_column_id)) {
            auto type = Schema::get_unordered_object_type(global_column_id);
            size_t length = Schema::get_unordered_object_length(global_column_id);
            auto sub_object_schema = Span<int32_t>{&schema[i] + 1, length};
            if (NodeType::StructuredArray == type) {
                int32_t sub_array_root
                        = m_global_schema_tree->find_matching_subtree_root_in_subtree(
                                array_root,
                                get_first_column_in_span(sub_object_schema),
                                NodeType::StructuredArray
                        );
                m_json_serializer.add_op(JsonSerializer::Op::BeginArrayDocument);
                column_idx = generate_structured_array_template(
                        sub_array_root,
                        column_idx,
                        sub_object_schema
                );
                m_json_serializer.add_op(JsonSerializer::Op::EndArray);
            } else if (NodeType::Object == type) {
                int32_t object_root = m_global_schema_tree->find_matching_subtree_root_in_subtree(
                        array_root,
                        get_first_column_in_span(sub_object_schema),
                        NodeType::Object
                );
                m_json_serializer.add_op(JsonSerializer::Op::BeginDocument);
                column_idx = generate_structured_object_template(
                        object_root,
                        column_idx,
                        sub_object_schema
                );
                m_json_serializer.add_op(JsonSerializer::Op::EndObject);
            }
            i += length;
        } else {
            auto node = m_global_schema_tree->get_node(global_column_id);
            switch (node->get_type()) {
                case NodeType::Object: {
                    find_intersection_and_fix_brackets(
                            array_root,
                            node->get_id(),
                            path_to_intersection
                    );
                    for (int j = 0; j < (node->get_depth() - depth); ++j) {
                        m_json_serializer.add_op(JsonSerializer::Op::EndObject);
                    }
                    break;
                }
                case NodeType::StructuredArray: {
                    m_json_serializer.add_op(JsonSerializer::Op::BeginArrayDocument);
                    m_json_serializer.add_op(JsonSerializer::Op::EndArray);
                    break;
                }
                case NodeType::Integer: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddIntField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::Float: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddFloatField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::Boolean: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddBoolField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::ClpString:
                case NodeType::VarString: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddStringField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::NullValue: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddNullValue);
                    break;
                }
                case NodeType::DateString:
                case NodeType::UnstructuredArray:
                case NodeType::Unknown:
                    break;
            }
        }
    }
    return column_idx;
}

size_t SchemaReader::generate_structured_object_template(
        int32_t object_root,
        size_t column_start,
        Span<int32_t> schema
) {
    int32_t root = object_root;
    size_t column_idx = column_start;
    std::vector<int32_t> path_to_intersection;

    for (size_t i = 0; i < schema.size(); ++i) {
        int32_t global_column_id = schema[i];
        if (Schema::schema_entry_is_unordered_object(global_column_id)) {
            // It should only be possible to encounter arrays inside of structured objects
            size_t array_length = Schema::get_unordered_object_length(global_column_id);
            auto array_schema = Span<int32_t>{&schema[i] + 1, array_length};
            // we can guarantee that the last array we hit on the path to object root must be the
            // right one because otherwise we'd be inside the structured array generator
            int32_t array_root = m_global_schema_tree->find_matching_subtree_root_in_subtree(
                    object_root,
                    get_first_column_in_span(array_schema),
                    NodeType::StructuredArray
            );

            find_intersection_and_fix_brackets(root, array_root, path_to_intersection);
            column_idx = generate_structured_array_template(array_root, column_idx, array_schema);
            m_json_serializer.add_op(JsonSerializer::Op::EndArray);
            i += array_length;
            // root is parent of the array object since we close the array bracket above
            auto node = m_global_schema_tree->get_node(array_root);
            root = node->get_parent_id();
        } else {
            auto node = m_global_schema_tree->get_node(global_column_id);
            int32_t next_root = node->get_parent_id();
            find_intersection_and_fix_brackets(root, next_root, path_to_intersection);
            root = next_root;
            switch (node->get_type()) {
                case NodeType::Object: {
                    m_json_serializer.add_op(JsonSerializer::Op::BeginObject);
                    m_json_serializer.add_special_key(node->get_key_name());
                    m_json_serializer.add_op(JsonSerializer::Op::EndObject);
                    break;
                }
                case NodeType::StructuredArray: {
                    m_json_serializer.add_op(JsonSerializer::Op::BeginArray);
                    m_json_serializer.add_special_key(node->get_key_name());
                    m_json_serializer.add_op(JsonSerializer::Op::EndArray);
                    break;
                }
                case NodeType::Integer: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddIntField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::Float: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddFloatField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::Boolean: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddBoolField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::ClpString:
                case NodeType::VarString: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddStringField);
                    m_reordered_columns.push_back(m_columns[column_idx++]);
                    break;
                }
                case NodeType::NullValue: {
                    m_json_serializer.add_op(JsonSerializer::Op::AddNullField);
                    m_json_serializer.add_special_key(node->get_key_name());
                    break;
                }
                case NodeType::DateString:
                case NodeType::UnstructuredArray:
                case NodeType::Unknown:
                    break;
            }
        }
    }
    find_intersection_and_fix_brackets(root, object_root, path_to_intersection);
    return column_idx;
}

void SchemaReader::generate_json_template(int32_t id) {
    auto node = m_local_schema_tree->get_node(id);
    auto children_ids = node->get_children_ids();

    for (int32_t child_id : children_ids) {
        int32_t child_global_id = m_local_id_to_global_id[child_id];
        auto child_node = m_local_schema_tree->get_node(child_id);
        std::string const& key = child_node->get_key_name();
        switch (child_node->get_type()) {
            case NodeType::Object: {
                m_json_serializer.add_op(JsonSerializer::Op::BeginObject);
                m_json_serializer.add_special_key(key);
                generate_json_template(child_id);
                m_json_serializer.add_op(JsonSerializer::Op::EndObject);
                break;
            }
            case NodeType::UnstructuredArray: {
                m_json_serializer.add_op(JsonSerializer::Op::AddArrayField);
                m_reordered_columns.push_back(m_column_map[child_global_id]);
                break;
            }
            case NodeType::StructuredArray: {
                m_json_serializer.add_op(JsonSerializer::Op::BeginArray);
                m_json_serializer.add_special_key(child_node->get_key_name());
                int32_t global_child_id = m_local_id_to_global_id[child_id];
                auto structured_it = m_global_id_to_unordered_object.find(global_child_id);
                if (m_global_id_to_unordered_object.end() != structured_it) {
                    size_t column_start = structured_it->second.first;
                    Span<int32_t> structured_schema = structured_it->second.second;
                    generate_structured_array_template(
                            global_child_id,
                            column_start,
                            structured_schema
                    );
                }
                m_json_serializer.add_op(JsonSerializer::Op::EndArray);
                break;
            }
            case NodeType::Integer: {
                m_json_serializer.add_op(JsonSerializer::Op::AddIntField);
                m_reordered_columns.push_back(m_column_map[child_global_id]);
                break;
            }
            case NodeType::Float: {
                m_json_serializer.add_op(JsonSerializer::Op::AddFloatField);
                m_reordered_columns.push_back(m_column_map[child_global_id]);
                break;
            }
            case NodeType::Boolean: {
                m_json_serializer.add_op(JsonSerializer::Op::AddBoolField);
                m_reordered_columns.push_back(m_column_map[child_global_id]);
                break;
            }
            case NodeType::ClpString:
            case NodeType::VarString:
            case NodeType::DateString: {
                m_json_serializer.add_op(JsonSerializer::Op::AddStringField);
                m_reordered_columns.push_back(m_column_map[child_global_id]);
                break;
            }
            case NodeType::NullValue: {
                m_json_serializer.add_op(JsonSerializer::Op::AddNullField);
                m_json_serializer.add_special_key(key);
                break;
            }
            case NodeType::Unknown:
                break;
        }
    }
}
}  // namespace clp_s
