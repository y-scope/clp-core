import {COMPRESSION_JOBS_TABLE_COLUMN_NAMES} from "../constants";


/**
 * Class for retrieving compression jobs from the database.
 */
class CompressionDbManager {
    #sqlDbConnPool;

    #compressionJobsTableName;

    /**
     * @param {import("mysql2/promise").Pool} sqlDbConnPool
     * @param {object} tableNames
     * @param {string} tableNames.compressionJobsTableName
     */
    constructor (sqlDbConnPool, {compressionJobsTableName}) {
        this.#sqlDbConnPool = sqlDbConnPool;
        this.#compressionJobsTableName = compressionJobsTableName;
    }

    /**
     * Retrieves the last `limit` number of jobs and the ones with the given
     * job IDs.
     *
     * @param {string} lastUpdateDate
     * @param {number[]} jobIds
     * @return {Promise<object[]>} Job objects with fields with the names in
     * `COMPRESSION_JOBS_TABLE_COLUMN_NAMES`
     */
    async getCompressionJobs (lastUpdateDate, jobIds) {
        const queries = [];

        queries.push(`
            WITH SelectedColumns AS (
                SELECT 
                    id as _id,
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.STATUS},
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.STATUS_MSG},
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.START_TIME},
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.UPDATE_TIME},
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.DURATION},
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.UNCOMPRESSED_SIZE},
                    ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.COMPRESSED_SIZE}
                FROM ${this.#compressionJobsTableName}
            )
            (
                SELECT *
                FROM SelectedColumns
                ORDER BY _id DESC
            )
        `);

        // The initial select may not include the jobs specified by `jobIds`, so we select
        // them explicitly and then deduplicate the rows with a UNION DISTINCT clause.
        jobIds.forEach((jobId) => {
            queries.push(`
                UNION DISTINCT
                (
                    SELECT *
                    FROM SelectedColumns
                    WHERE 
                        _id=${jobId} && 
                        ${COMPRESSION_JOBS_TABLE_COLUMN_NAMES.UPDATE_TIME} >= '${lastUpdateDate}'
                )
            `);
        });

        queries.push("ORDER BY _id DESC;");

        const queryString = queries.join("\n");
        const [results] = await this.#sqlDbConnPool.query(queryString);

        return results;
    }
}

export default CompressionDbManager;
