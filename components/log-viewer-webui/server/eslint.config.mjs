import CommonConfig from "eslint-config-yscope/CommonConfig.mjs";
import StylisticConfigArray from "eslint-config-yscope/StylisticConfigArray.mjs";
import TsConfigArray from "eslint-config-yscope/TsConfigArray.mjs";


const EslintConfig = [
    {
        ignores: [
            "dist/",
            "node_modules/",
        ],
    },
    CommonConfig,
    ...TsConfigArray.map(
        (config) => ({
            files: [
                "**/*.ts",
            ],
            ...config,
        })
    ),
    ...StylisticConfigArray,
    {
        rules: {
            "@typescript-eslint/require-await": [
                // Fastify recommends async syntax, but not all plugins require Promise resolution
                // in their function bodies.
                "off",
            ],
            "new-cap": [
                "error",
                {
                    capIsNewExceptions: [
                        "Type.Enum",
                        "Type.Integer",
                    ],
                },
            ],
        },
    },
];


export default EslintConfig;
