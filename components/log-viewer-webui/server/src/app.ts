import process from "node:process";

import {
    fastify,
    FastifyInstance,
    FastifyServerOptions,
} from "fastify";

import settings from "../settings.json" with {type: "json"};
import DbManager from "./DbManager.js";
import exampleRoutes from "./routes/example.js";
import queryRoutes from "./routes/query.js";
import staticRoutes from "./routes/static.js";


interface AppProps {
    fastifyOptions: FastifyServerOptions;
    sqlDbUser: string;
    sqlDbPass: string;
}

/**
 * Creates the Fastify app with the given options.
 *
 * @param props
 * @param props.fastifyOptions
 * @param props.sqlDbUser
 * @param props.sqlDbPass
 * @return The created Fastify instance.
 */
const app = async ({
    fastifyOptions,
    sqlDbUser,
    sqlDbPass,
}: AppProps): Promise<FastifyInstance> => {
    const server = fastify(fastifyOptions);

    if ("test" !== process.env.NODE_ENV) {
        await server.register(DbManager, {
            mysqlConfig: {
                database: settings.SqlDbName,
                host: settings.SqlDbHost,
                password: sqlDbPass,
                port: settings.SqlDbPort,
                queryJobsTableName: settings.SqlDbQueryJobsTableName,
                user: sqlDbUser,
            },
            mongoConfig: {
                database: settings.MongoDbName,
                host: settings.MongoDbHost,
                streamFilesCollectionName: settings.MongoDbStreamFilesCollectionName,
                port: settings.MongoDbPort,
            },
        });
    }

    await server.register(staticRoutes);
    await server.register(exampleRoutes);
    await server.register(queryRoutes);

    return server;
};

export default app;
