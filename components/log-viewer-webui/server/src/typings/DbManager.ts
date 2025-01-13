import {RowDataPacket} from "mysql2/promise";


/**
 * Matching the `QueryJobType` class in `job_orchestration.query_scheduler.constants`.
 */
enum QUERY_JOB_TYPE {
    SEARCH_OR_AGGREGATION = 0,
    EXTRACT_IR,
    EXTRACT_JSON,
}

/**
 * List of valid extract job types.
 */
const EXTRACT_JOB_TYPES = new Set([
    QUERY_JOB_TYPE.EXTRACT_IR,
    QUERY_JOB_TYPE.EXTRACT_JSON,
]);

/**
 * Matching the `QueryJobStatus` class in
 * `job_orchestration.query_scheduler.constants`.
 *
 * @enum {number}
 */
enum QUERY_JOB_STATUS {
    PENDING = 0,
    RUNNING,
    SUCCEEDED,
    FAILED,
    CANCELLING,
    CANCELLED,
}

/**
 * List of states that indicate the job is either pending or in progress.
 */
const QUERY_JOB_STATUS_WAITING_STATES = new Set([
    QUERY_JOB_STATUS.PENDING,
    QUERY_JOB_STATUS.RUNNING,
    QUERY_JOB_STATUS.CANCELLING,
]);

/**
 * The `query_jobs` table's column names.
 *
 * @enum {string}
 */
enum QUERY_JOBS_TABLE_COLUMN_NAMES {
    ID = "id",
    TYPE = "type",
    STATUS = "status",
    JOB_CONFIG = "job_config",
}


interface QueryJob extends RowDataPacket {
    [QUERY_JOBS_TABLE_COLUMN_NAMES.ID]: number;
    [QUERY_JOBS_TABLE_COLUMN_NAMES.TYPE]: QUERY_JOB_TYPE;
    [QUERY_JOBS_TABLE_COLUMN_NAMES.STATUS]: QUERY_JOB_STATUS;
    [QUERY_JOBS_TABLE_COLUMN_NAMES.JOB_CONFIG]: string;
}


export type {QueryJob};
export {
    EXTRACT_JOB_TYPES,
    QUERY_JOB_STATUS,
    QUERY_JOB_STATUS_WAITING_STATES,
    QUERY_JOB_TYPE,
    QUERY_JOBS_TABLE_COLUMN_NAMES,
};
