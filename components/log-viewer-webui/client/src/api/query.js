import axios from "axios";


/**
 * @typedef {object} ExtractIrResp
 * @property {number} begin_msg_ix
 * @property {number} end_msg_ix
 * @property {string} file_split_id
 * @property {boolean} is_last_ir_chunk
 * @property {string} orig_file_id
 * @property {string} path
 * @property {string} _id
 */

/**
 * Submits a job to extract the stream that contains a given log event. The stream is extracted
 * either as a CLP IR or a JSON line file.
 *
 * @param {number} extractJobType Type of the extraction job
 * @param {number|string} streamId The ID of the original stream
 * @param {number} logEventIdx The index of the log event
 * @param {Function} onUploadProgress Callback to handle upload progress events.
 * @return {Promise<axios.AxiosResponse<ExtractIrResp>>}
 */
const submitExtractStreamJob = async (extractJobType, streamId, logEventIdx, onUploadProgress) => {
    return await axios.post(
        "/query/extract-stream",
        {extractJobType, streamId, logEventIdx},
        {onUploadProgress}
    );
};

export {submitExtractStreamJob};
