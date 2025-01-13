import fastifyMongo from "@fastify/mongodb";
import {
    fastifyMysql,
    MySQLPromisePool,
} from "@fastify/mysql";
import {encode as msgpackEncode} from "@msgpack/msgpack";
import {
    FastifyInstance,
    FastifyPluginAsync,
} from "fastify";
import {fastifyPlugin} from "fastify-plugin";
import {Collection} from "mongodb";
import {
    Pool as PromisePool,
    ResultSetHeader,
} from "mysql2/promise";

import {
    QUERY_JOB_STATUS,
    QUERY_JOB_STATUS_WAITING_STATES,
    QUERY_JOB_TYPE,
    QUERY_JOBS_TABLE_COLUMN_NAMES,
    QueryJob,
} from "./typings/DbManager.js";
import {sleep} from "./utils.js";


interface DbManagerOptions {
    mysqlConfig: {
        user: string;
        password: string;
        host: string;
        port: number;
        database: string;
        queryJobsTableName: string;
    };
    mongoConfig: {
        database: string;
        host: string;
        streamFilesCollectionName: string;
        port: number;
    };
}

interface StreamFileMongoDocument {
    path: string;
    stream_id: string;
    begin_msg_ix: number;
    end_msg_ix: number;
    is_last_chunk: boolean;
}

type StreamFilesCollection = Collection<StreamFileMongoDocument>;

/**
 * Interval in milliseconds for polling the completion status of a job.
 */
const JOB_COMPLETION_STATUS_POLL_INTERVAL_MILLIS = 0.5;


/**
 * Class to manage connections to the jobs database (MySQL) and results cache (MongoDB).
 */
class DbManager {
    #fastify;

    readonly #mysqlConnectionPool: PromisePool;

    readonly #streamFilesCollection: StreamFilesCollection;

    readonly #queryJobsTableName: string;

    /**
     * @param props
     * @param props.app
     * @param props.mysqlConnectionPool
     * @param props.queryJobsTableName
     * @param props.streamFilesCollection
     */
    constructor ({app, mysqlConnectionPool, queryJobsTableName, streamFilesCollection}: {
        app: FastifyInstance;
        mysqlConnectionPool: PromisePool;
        queryJobsTableName: string;
        streamFilesCollection: StreamFilesCollection;
    }) {
        this.#fastify = app;

        this.#mysqlConnectionPool = mysqlConnectionPool;
        this.#streamFilesCollection = streamFilesCollection;

        this.#queryJobsTableName = queryJobsTableName;
    }

    static async create (app: FastifyInstance, dbConfig: DbManagerOptions) {
        const mysqlConnectionPool = await DbManager.#initMySql(app, dbConfig.mysqlConfig);
        const {streamFilesCollection} = await DbManager.#initMongo(app, dbConfig.mongoConfig);
        return new DbManager({
            app: app,
            mysqlConnectionPool: mysqlConnectionPool,
            streamFilesCollection: streamFilesCollection,
            queryJobsTableName: dbConfig.mysqlConfig.queryJobsTableName,
        });
    }

    /**
     * Initializes the MySQL plugin.
     *
     * @param app
     * @param config
     * @return The MySQL connection pool created during initialization.
     */
    static async #initMySql (app: FastifyInstance, config: DbManagerOptions["mysqlConfig"]) {
        await app.register(fastifyMysql, {
            promise: true,
            connectionString: `mysql://${config.user}:${config.password}@${config.host}:` +
                `${config.port}/${config.database}`,
        });

        return app.mysql.pool;
    }

    /**
     * Initializes the MongoDB plugin.
     *
     * @param app
     * @param config
     * @return The MongoDB Collection objects created during initialization.
     */
    static async #initMongo (app: FastifyInstance, config: DbManagerOptions["mongoConfig"])
        : Promise<{
            streamFilesCollection: StreamFilesCollection;
        }> {
        await app.register(fastifyMongo, {
            forceClose: true,
            url: `mongodb://${config.host}:${config.port}/${config.database}`,
        });
        if ("undefined" === typeof app.mongo.db) {
            throw new Error("Failed to initialize MongoDB plugin.");
        }

        return {streamFilesCollection: app.mongo.db.collection(config.streamFilesCollectionName)};
    }


    /**
     * Submits a stream extraction job to the scheduler and waits for it to finish.
     *
     * @param props
     * @param props.jobType
     * @param props.logEventIdx
     * @param props.streamId
     * @param props.targetUncompressedSize
     * @return The ID of the job or null if an error occurred.
     */
    async submitAndWaitForExtractStreamJob ({
        jobType,
        logEventIdx,
        streamId,
        targetUncompressedSize,
    }: {
        jobType: QUERY_JOB_TYPE;
        logEventIdx: number;
        streamId: string;
        targetUncompressedSize: number;
    }): Promise<number | null> {
        let jobConfig;
        if (QUERY_JOB_TYPE.EXTRACT_IR === jobType) {
            jobConfig = {
                file_split_id: null,
                msg_ix: logEventIdx,
                orig_file_id: streamId,
                target_uncompressed_size: targetUncompressedSize,
            };
        } else if (QUERY_JOB_TYPE.EXTRACT_JSON === jobType) {
            jobConfig = {
                archive_id: streamId,
                target_chunk_size: targetUncompressedSize,
            };
        }

        let jobId;
        try {
            const [result] = await this.#mysqlConnectionPool.query<ResultSetHeader>(
                `INSERT INTO ${this.#queryJobsTableName} (job_config, type)
                 VALUES (?, ?)`,
                [
                    Buffer.from(msgpackEncode(jobConfig)),
                    jobType,
                ]
            );

            ({insertId: jobId} = result);
            await this.#awaitJobCompletion(jobId);
        } catch (e) {
            this.#fastify.log.error(e);

            return null;
        }

        return jobId;
    }

    /**
     * Gets the metadata for the extracted stream that has the given streamId and contains the
     * given logEventIdx.
     *
     * @param streamId
     * @param logEventIdx
     * @return A promise that resolves to the extracted stream's metadata.
     */
    async getExtractedStreamFileMetadata (streamId: string, logEventIdx: number)
        : Promise<StreamFileMongoDocument | null> {
        return await this.#streamFilesCollection.findOne({
            stream_id: streamId,
            begin_msg_ix: {$lte: logEventIdx},
            end_msg_ix: {$gt: logEventIdx},
        });
    }

    /**
     * Waits for the job with the given ID to finish.
     *
     * @param jobId
     * @throws {Error} If there's an error querying the job's status, the job is not found in the
     * database, the job was cancelled, or it exited with an unexpected status.
     */
    async #awaitJobCompletion (jobId: number) {
        // eslint-disable-next-line @typescript-eslint/no-unnecessary-condition
        while (true) {
            let rows: QueryJob[];
            try {
                const [queryResult] = await this.#mysqlConnectionPool.query<QueryJob[]>(
                    `
                        SELECT ${QUERY_JOBS_TABLE_COLUMN_NAMES.STATUS}
                        FROM ${this.#queryJobsTableName}
                        WHERE ${QUERY_JOBS_TABLE_COLUMN_NAMES.ID} = ?
                    `,
                    jobId,
                );

                rows = queryResult;
            } catch (e: unknown) {
                throw new Error(`Failed to query status for job ${jobId} - ${e?.toString()}`);
            }

            const [job] = rows;
            if ("undefined" === typeof job) {
                throw new Error(`Job ${jobId} not found in database.`);
            }
            const status = job[QUERY_JOBS_TABLE_COLUMN_NAMES.STATUS];

            if (false === QUERY_JOB_STATUS_WAITING_STATES.has(status)) {
                if (QUERY_JOB_STATUS.CANCELLED === status) {
                    throw new Error(`Job ${jobId} was cancelled.`);
                } else if (QUERY_JOB_STATUS.SUCCEEDED !== status) {
                    throw new Error(`Job ${jobId} exited with unexpected status=${status}: ` +
                        `${Object.keys(QUERY_JOB_STATUS)[status]}.`);
                }
                break;
            }

            await sleep(JOB_COMPLETION_STATUS_POLL_INTERVAL_MILLIS);
        }
    }
}

/**
 * A Fastify plugin callback for setting up the `DbManager`.
 *
 * @param app
 * @param options
 */
const dbManagerPluginCallback: FastifyPluginAsync<DbManagerOptions> = async (app, options) => {
    const dbManager = await DbManager.create(app, options);
    app.decorate("dbManager", dbManager);
};

declare module "fastify" {
    interface FastifyInstance {

        // The typing of `@fastify/mysql` needs to be manually specified.
        // See https://github.com/fastify/fastify-mysql#typescript
        mysql: MySQLPromisePool;

        dbManager: DbManager;
    }
}


export {QUERY_JOB_TYPE};
export default fastifyPlugin(dbManagerPluginCallback);
