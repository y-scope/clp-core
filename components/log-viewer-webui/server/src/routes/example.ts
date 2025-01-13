import {TypeBoxTypeProvider} from "@fastify/type-provider-typebox";
import {Type} from "@sinclair/typebox";
import {FastifyPluginAsync} from "fastify";


/**
 * Creates example routes.
 *
 * @param app
 */
const routes: FastifyPluginAsync = async (app) => {
    const fastify = app.withTypeProvider<TypeBoxTypeProvider>();

    fastify.get("/example/get/:name", {
        schema: {
            params: Type.Object({
                name: Type.String(),
            }),
        },
    }, async (req) => {
        return {msg: `Hello, ${req.params.name}!`};
    });

    fastify.post("/example/post", {
        schema: {
            body: Type.Object({
                name: Type.String(),
            }),
        },
    }, async (req) => {
        return {msg: `Goodbye, ${req.body.name}!`};
    });
};

export default routes;
