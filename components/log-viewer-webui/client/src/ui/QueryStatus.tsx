import {
    useEffect,
    useRef,
    useState,
} from "react";

import {Static} from "@sinclair/typebox";
import {
    AssertError,
    Value,
} from "@sinclair/typebox/value";
import {isAxiosError} from "axios";

import {submitExtractStreamJob} from "../api/query";
import {Nullable} from "../typings/common";
import {
    ExtractJobSearchParams,
    QUERY_LOADING_STATE,
} from "../typings/query";
import Loading from "./Loading";


/**
 * Submits queries and renders the query states.
 *
 * @return
 */
const QueryStatus = () => {
    const [queryState, setQueryState] = useState<QUERY_LOADING_STATE>(
        QUERY_LOADING_STATE.SUBMITTING
    );
    const [errorMsg, setErrorMsg] = useState<Nullable<string>>(null);
    const isFirstRun = useRef(true);

    useEffect(() => {
        if (false === isFirstRun.current) {
            return;
        }
        isFirstRun.current = false;

        // Validates and parse search parameters.
        const searchParams = new URLSearchParams(window.location.search);
        const paramsObj = Object.fromEntries(searchParams);
        let parseResult: Static<typeof ExtractJobSearchParams>;
        try {
            parseResult = Value.Parse(ExtractJobSearchParams, paramsObj);
            console.log(parseResult);
        } catch (e: unknown) {
            let error = "URL parameters parsing failed";
            if (e instanceof AssertError) {
                error += `: ${e.message}`;
            }
            console.error(error);
            setErrorMsg(error);

            return;
        }

        submitExtractStreamJob(
            parseResult.type,
            parseResult.streamId,
            parseResult.logEventIdx,
            () => {
                setQueryState(QUERY_LOADING_STATE.WAITING);
            }
        )
            .then(({data}) => {
                setQueryState(QUERY_LOADING_STATE.LOADING);

                const innerLogEventNum = parseResult.logEventIdx - data.begin_msg_ix + 1;
                window.location.href = `/log-viewer/index.html?filePath=/streams/${data.path}` +
                    `#logEventNum=${innerLogEventNum}`;
            })
            .catch((e: unknown) => {
                let msg = "Unknown error.";
                if (isAxiosError<{message?: string}>(e)) {
                    msg = e.message;
                    if ("undefined" !== typeof e.response) {
                        if ("undefined" !== typeof e.response.data.message) {
                            msg = e.response.data.message;
                        } else {
                            msg = e.response.statusText;
                        }
                    }
                }
                console.error(msg, e);
                setErrorMsg(msg);
            });
    }, []);

    return (
        <Loading
            currentState={queryState}
            errorMsg={errorMsg}/>
    );
};

export default QueryStatus;
