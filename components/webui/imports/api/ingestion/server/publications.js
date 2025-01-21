import {Meteor} from "meteor/meteor";

import {logger} from "/imports/utils/logger";
import {MONGO_SORT_BY_ID} from "/imports/utils/mongo";

import {
    CompressionJobsCollection,
    STATS_COLLECTION_ID,
    StatsCollection,
} from "../collections";
import {
    COMPRESSION_JOB_WAITING_STATES,
    COMPRESSION_JOBS_TABLE_COLUMN_NAMES,
} from "../constants";
import CompressionDbManager from "./CompressionDbManager";
import StatsDbManager from "./StatsDbManager";


const COMPRESSION_JOBS_REFRESH_INTERVAL_MILLIS = 1000;

const STATS_REFRESH_INTERVAL_MILLIS = 5000;

const CONST_FOR_DATE_FORMAT = 19;

/**
 * @type {CompressionDbManager|null}
 */
let compressionDbManager = null;

/**
 * @type {StatsDbManager|null}
 */
let statsDbManager = null;

/**
 * @type {number|null}
 */
let compressionJobsRefreshTimeout = null;

/**
 * @type {number|null}
 */
let statsRefreshInterval = null;

/**
 * @type {string}
 */
let lastUpdateDate = new Date().toISOString()
    .slice(0, CONST_FOR_DATE_FORMAT)
    .replace("T", " ");

/**
 * Updates the compression statistics in the StatsCollection.
 *
 * @return {Promise<void>}
 */
const refreshCompressionStats = async () => {
    if (0 === Meteor.server.stream_server.all_sockets().length) {
        return;
    }

    const stats = await statsDbManager.getCompressionStats();
    const filter = {
        _id: STATS_COLLECTION_ID.COMPRESSION,
    };
    const modifier = {
        $set: stats,
    };
    const options = {
        upsert: true,
    };

    await StatsCollection.updateAsync(filter, modifier, options);
};

/**
 * Updates the compression jobs in the CompressionJobsCollection.
 *
 * @return {Promise<void>}
 */
const refreshCompressionJobs = async () => {
    if (null !== compressionJobsRefreshTimeout) {
        // Clear the timeout in case this method is not called due to the timeout expiring.
        Meteor.clearTimeout(compressionJobsRefreshTimeout);
        compressionJobsRefreshTimeout = null;
    }
    if (0 === Meteor.server.stream_server.all_sockets().length) {
        compressionJobsRefreshTimeout = Meteor.setTimeout(
            refreshCompressionJobs,
            COMPRESSION_JOBS_REFRESH_INTERVAL_MILLIS
        );

        return;
    }

    const pendingJobIds = await CompressionJobsCollection.find({
        [COMPRESSION_JOBS_TABLE_COLUMN_NAMES.STATUS]: {
            $in: COMPRESSION_JOB_WAITING_STATES,
        },
    })
        .fetch()
        .map((job) => (
            job._id
        ));

    const previousUpdateDate = lastUpdateDate;
    lastUpdateDate = new Date().toISOString()
        .slice(0, CONST_FOR_DATE_FORMAT)
        .replace("T", " ");

    const jobs = await compressionDbManager.getCompressionJobs(
        previousUpdateDate,
        pendingJobIds
    );

    const operations = jobs.map((doc) => ({
        updateOne: {
            filter: {_id: doc._id},
            update: {$set: doc},
            upsert: true,
        },
    }));

    if (0 !== operations.length) {
        await CompressionJobsCollection.rawCollection().bulkWrite(operations);
    }

    // `refreshCompressionJobs()` shall not be run concurrently and therefore incurs no race
    // condition.
    // eslint-disable-next-line require-atomic-updates
    compressionJobsRefreshTimeout = Meteor.setTimeout(
        refreshCompressionJobs,
        COMPRESSION_JOBS_REFRESH_INTERVAL_MILLIS
    );
};

/**
 * Initializes the CompressionDbManager and starts a timeout timer (`compressionJobsRefreshTimeout`)
 * for compression job updates.
 *
 * @param {import("mysql2/promise").Pool} sqlDbConnPool
 * @param {object} tableNames
 * @param {string} tableNames.compressionJobsTableName
 * @throws {Error} on error.
 */
const initCompressionDbManager = (sqlDbConnPool, {
    compressionJobsTableName,
}) => {
    compressionDbManager = new CompressionDbManager(sqlDbConnPool, {
        compressionJobsTableName,
    });

    compressionJobsRefreshTimeout = Meteor.setTimeout(
        refreshCompressionJobs,
        COMPRESSION_JOBS_REFRESH_INTERVAL_MILLIS
    );
};

/**
 * De-initializes the CompressionDbManager by clearing the timeout timer for compression job
 * updates (`refreshCompressionJobs`).
 */
const deinitCompressionDbManager = () => {
    if (null !== compressionJobsRefreshTimeout) {
        Meteor.clearTimeout(compressionJobsRefreshTimeout);
        compressionJobsRefreshTimeout = null;
    }
};

/**
 * Initializes the StatsDbManager and starts an interval timer (`refreshMeteorInterval`) for
 * compression stats updates.
 *
 * @param {import("mysql2/promise").Pool} sqlDbConnPool
 * @param {object} tableNames
 * @param {string} tableNames.clpArchivesTableName
 * @param {string} tableNames.clpFilesTableName
 * @throws {Error} on error.
 */
const initStatsDbManager = (sqlDbConnPool, {
    clpArchivesTableName,
    clpFilesTableName,
}) => {
    statsDbManager = new StatsDbManager(sqlDbConnPool, {
        clpArchivesTableName,
        clpFilesTableName,
    });

    statsRefreshInterval = Meteor.setInterval(
        refreshCompressionStats,
        STATS_REFRESH_INTERVAL_MILLIS
    );
};

/**
 * De-initializes the StatsDbManager by clearing the interval timer for compression stats updates
 * (`refreshMeteorInterval`).
 */
const deinitStatsDbManager = () => {
    if (null !== statsRefreshInterval) {
        Meteor.clearInterval(statsRefreshInterval);
        statsRefreshInterval = null;
    }
};

/**
 * Updates and publishes compression job statuses.
 *
 * @param {string} publicationName
 * @return {Mongo.Cursor}
 */
Meteor.publish(Meteor.settings.public.CompressionJobsCollectionName, async () => {
    logger.debug(`Subscription '${Meteor.settings.public.CompressionJobsCollectionName}'`);

    await refreshCompressionJobs();

    const findOptions = {
        disableOplog: true,
        pollingIntervalMs: COMPRESSION_JOBS_REFRESH_INTERVAL_MILLIS,
        sort: [MONGO_SORT_BY_ID],
    };

    return CompressionJobsCollection.find({}, findOptions);
});

/**
 * Updates and publishes compression statistics.
 *
 * @param {string} publicationName
 * @return {Mongo.Cursor}
 */
Meteor.publish(Meteor.settings.public.StatsCollectionName, async () => {
    logger.debug(`Subscription '${Meteor.settings.public.StatsCollectionName}'`);

    await refreshCompressionStats();

    const filter = {
        _id: STATS_COLLECTION_ID.COMPRESSION,
    };

    return StatsCollection.find(filter);
});

export {
    deinitCompressionDbManager,
    deinitStatsDbManager,
    initCompressionDbManager,
    initStatsDbManager,
};
