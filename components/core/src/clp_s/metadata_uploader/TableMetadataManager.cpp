#include "TableMetadataManager.hpp"

#include <filesystem>

namespace clp_s::metadata_uploader {
TableMetadataManager::TableMetadataManager(
        std::optional<clp::GlobalMetadataDBConfig> const& db_config
) {
    if (db_config.has_value()) {
        m_table_metadata_db = std::make_unique<MySQLTableMetadataDB>(
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

TableMetadataManager::~TableMetadataManager() {
    if (m_output_type == OutputType::Database) {
        m_table_metadata_db->close();
    }
}

void TableMetadataManager::update_metadata(
        std::string const& archive_dir,
        std::string const& archive_id
) {
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

std::vector<std::pair<std::string, clp_s::NodeType>> TableMetadataManager::traverse_schema_tree(
        std::shared_ptr<SchemaTree> const& schema_tree
) {
    std::vector<std::pair<std::string, clp_s::NodeType>> fields;
    if (nullptr == schema_tree) {
        return fields;
    }

    std::string path_buffer;
    // Stack of pairs of node_id and path_length
    std::stack<std::pair<int32_t, uint64_t>> s;
    for (auto& node : schema_tree->get_nodes()) {
        if (constants::cRootNodeId == node.get_parent_id() && clp_s::NodeType::Metadata != node.get_type()) {
            s.push({node.get_id(), 0});
            break;
        }
    }

    while (!s.empty()) {
        auto [node_id, path_length] = s.top();
        s.pop();

        auto& node = schema_tree->get_node(node_id);
        auto& children_ids = node.get_children_ids();
        auto node_type = node.get_type();
        path_buffer.resize(path_length);
        if (false == path_buffer.empty()) {
            path_buffer += ".";
        }
        path_buffer += node.get_key_name();
        if (children_ids.empty() && clp_s::NodeType::Object != node_type
            && clp_s::NodeType::Unknown != node_type)
        {
            fields.push_back({path_buffer, node_type});
        }

        for (auto child_id : children_ids) {
            s.push({child_id, path_buffer.size()});
        }
    }

    return fields;
}
}  // namespace clp_s::metadata_uploader
