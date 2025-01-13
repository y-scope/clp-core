import axios, {
    AxiosProgressEvent,
    AxiosResponse,
} from "axios";

import {QUERY_JOB_TYPE} from "../typings/query";


interface ExtractStreamResp {
    _id: string;
    begin_msg_ix: number;
    end_msg_ix: number;
    is_last_chunk: boolean;
    path: string;
    stream_id: string;
}


/**
 * Submits a job to extract the stream that contains a given log event. The stream is extracted
 * either as a CLP IR or a JSON Lines file.
 *
 * @param extractJobType
 * @param streamId
 * @param logEventIdx
 * @param onUploadProgress Callback to handle upload progress events.
 * @return The API response.
 */
const submitExtractStreamJob = async (
    extractJobType: QUERY_JOB_TYPE,
    streamId: string,
    logEventIdx: number,
    onUploadProgress: (progressEvent: AxiosProgressEvent) => void,
): Promise<AxiosResponse<ExtractStreamResp>> => {
    return await axios.post(
        "/query/extract-stream",
        {
            extractJobType,
            streamId,
            logEventIdx,
        },
        {onUploadProgress}
    );
};

export {submitExtractStreamJob};
