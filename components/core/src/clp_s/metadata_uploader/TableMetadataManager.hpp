#ifndef CLP_S_METADATA_UPLOADER_TABLEMETADATAMANAGER_HPP
#define CLP_S_METADATA_UPLOADER_TABLEMETADATAMANAGER_HPP

#include "../../clp/GlobalMetadataDBConfig.hpp"
#include "../ArchiveReader.hpp"
#include "MySQLTableMetadataDB.hpp"

namespace clp_s::metadata_uploader {
/**
 * Class used to updates field names (e.g., JSON full paths) and data types for a specified archive
 * directory. It currently stores the results in a database. An archive directory consists of
 * multiple archives on the same topic, which can be queried using SQL. The directory name serves as
 * the table name, and its metadata (field names and data types) is used by the SQL engine to define
 * the table schema.
 */
class TableMetadataManager {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    enum class OutputType : uint8_t {
        Database
    };

    // Constructors
    TableMetadataManager(
            std::optional<clp::GlobalMetadataDBConfig> const& db_config = std::nullopt
    );

    // Destructor
    ~TableMetadataManager();

    // Methods
    /**
     * Updates the metadata for a given archive
     * @param archive_dir used as the table name
     * @param archive_id
     */
    void update_metadata(std::string const& archive_dir, std::string const& archive_id);

private:
    /**
     * Traverses the schema tree and returns a list of path names and their types
     * @param schema_tree
     * @return a list of path names and their types
     */
    std::vector<std::pair<std::string, clp_s::NodeType>> traverse_schema_tree(
            std::shared_ptr<SchemaTree> const& schema_tree
    );

    std::shared_ptr<MySQLTableMetadataDB> m_table_metadata_db;
    OutputType m_output_type{OutputType::Database};
};
}  // namespace clp_s::metadata_uploader
#endif  // CLP_S_METADATA_UPLOADER_TABLEMETADATAMANAGER_HPP
