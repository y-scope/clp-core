#include "IndexManager.hpp"

#include <filesystem>
#include <stack>

#include "../archive_constants.hpp"

namespace clp_s::indexer {
IndexManager::IndexManager(std::optional<clp::GlobalMetadataDBConfig> const& db_config) {
    if (db_config.has_value()) {
        m_table_metadata_db = std::make_unique<MySQLIndexStorage>(
                db_config->get_metadata_db_host(),
                db_config->get_metadata_db_port(),
                db_config->get_metadata_db_username(),
                db_config->get_metadata_db_password(),
                db_config->get_metadata_db_name(),
                db_config->get_metadata_table_prefix()
        );
        m_table_metadata_db->open();
        m_output_type = OutputType::Database;
    } else {
        throw OperationFailed(ErrorCodeBadParam, __FILENAME__, __LINE__);
    }
}

IndexManager::~IndexManager() {
    if (m_output_type == OutputType::Database) {
        m_table_metadata_db->close();
    }
}

void IndexManager::update_metadata(std::string const& archive_dir, std::string const& archive_id) {
    m_table_metadata_db->init(archive_dir);

    auto archive_path = std::filesystem::path(archive_dir) / archive_id;
    std::error_code ec;
    if (false == std::filesystem::exists(archive_path, ec) || ec) {
        throw OperationFailed(ErrorCodeBadParam, __FILENAME__, __LINE__);
    }

    ArchiveReader archive_reader;
    archive_reader.open(
            clp_s::Path{.source = clp_s::InputSource::Filesystem, .path = archive_path.string()},
            NetworkAuthOption{}
    );

    auto schema_tree = archive_reader.get_schema_tree();
    auto field_pairs = traverse_schema_tree(schema_tree);
    if (OutputType::Database == m_output_type) {
        for (auto& [name, type] : field_pairs) {
            m_table_metadata_db->add_field(name, type);
        }
    }
}

std::string IndexManager::escape_key_name(std::string_view const key_name) {
    std::string escaped_key_name;
    escaped_key_name.reserve(key_name.size());
    for (auto c : key_name) {
        switch (c) {
            case '\"':
                escaped_key_name += "\\\"";
                break;
            case '\\':
                escaped_key_name += "\\\\";
                break;
            case '\n':
                escaped_key_name += "\\n";
                break;
            case '\t':
                escaped_key_name += "\\t";
                break;
            case '\r':
                escaped_key_name += "\\r";
                break;
            case '\b':
                escaped_key_name += "\\b";
                break;
            case '\f':
                escaped_key_name += "\\f";
                break;
            case '.':
                escaped_key_name += "\\.";
                break;
            default:
                if (std::isprint(c)) {
                    escaped_key_name += c;
                } else {
                    char buffer[7];
                    std::snprintf(
                            buffer,
                            sizeof(buffer),
                            "\\u00%02x",
                            static_cast<unsigned char>(c)
                    );
                    escaped_key_name += buffer;
                }
        }
    }
    return escaped_key_name;
}

std::vector<std::pair<std::string, clp_s::NodeType>> IndexManager::traverse_schema_tree(
        std::shared_ptr<SchemaTree> const& schema_tree
) {
    std::vector<std::pair<std::string, clp_s::NodeType>> fields;
    if (nullptr == schema_tree) {
        return fields;
    }

    std::string path_buffer;
    // Stack of pairs of node_id and path_length
    std::stack<std::pair<int32_t, uint64_t>> s;
    for (auto const& node : schema_tree->get_nodes()) {
        if (constants::cRootNodeId == node.get_parent_id()
            && clp_s::NodeType::Metadata != node.get_type())
        {
            s.emplace(node.get_id(), 0);
            break;
        }
    }

    while (false == s.empty()) {
        auto [node_id, path_length] = s.top();
        s.pop();

        auto const& node = schema_tree->get_node(node_id);
        auto const& children_ids = node.get_children_ids();
        auto node_type = node.get_type();
        path_buffer.resize(path_length);
        if (false == path_buffer.empty()) {
            path_buffer += ".";
        }
        path_buffer += escape_key_name(node.get_key_name());
        if (children_ids.empty() && clp_s::NodeType::Object != node_type
            && clp_s::NodeType::Unknown != node_type)
        {
            fields.emplace_back(path_buffer, node_type);
        }

        for (auto child_id : children_ids) {
            s.emplace(child_id, path_buffer.size());
        }
    }

    return fields;
}
}  // namespace clp_s::indexer
