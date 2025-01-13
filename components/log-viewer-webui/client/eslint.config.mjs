import CommonConfig from "eslint-config-yscope/CommonConfig.mjs";
import ReactConfigArray from "eslint-config-yscope/ReactConfigArray.mjs";
import StylisticConfigArray from "eslint-config-yscope/StylisticConfigArray.mjs";
import TsConfigArray, {createTsConfigOverride} from "eslint-config-yscope/TsConfigArray.mjs";


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
                "**/*.tsx",
            ],
            ...config,
        })
    ),
    createTsConfigOverride(
        [
            "src/**/*.ts",
            "src/**/*.tsx",
        ],
        "tsconfig.app.json"
    ),
    createTsConfigOverride(
        ["vite.config.ts"],
        "tsconfig.node.json"
    ),
    ...StylisticConfigArray,
    ...ReactConfigArray,
    {
        rules: {
            "new-cap": [
                "error",
                {
                    capIsNewExceptions: [
                        "Decode",
                        "Encode",
                        "Type.Transform",
                        "Type.Union",
                        "Type.Literal",
                        "Value.Parse",
                    ],
                },
            ],
        },
    },
];


export default EslintConfig;
