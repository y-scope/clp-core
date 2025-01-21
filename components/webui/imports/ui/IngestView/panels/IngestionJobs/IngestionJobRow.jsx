import OverlayTrigger from "react-bootstrap/OverlayTrigger";
import Spinner from "react-bootstrap/Spinner";
import Tooltip from "react-bootstrap/Tooltip";

import dayjs from "dayjs";

import {
    faCheck,
    faClock,
    faExclamation,
} from "@fortawesome/free-solid-svg-icons";
import {FontAwesomeIcon} from "@fortawesome/react-fontawesome";

import {
    COMPRESSION_JOB_STATUS,
    COMPRESSION_JOB_STATUS_NAMES,
} from "/imports/api/ingestion/constants";
import {computeHumanSize} from "/imports/utils/misc";

import PlaceholderText from "./PlaceholderText";


/**
 * Icons corresponding to different compression job statuses.
 *
 * @type {{[key: CompressionJobStatus]: import("@fortawesome/react-fontawesome").IconProp}}
 */
const COMPRESSION_JOB_STATUS_ICONS = Object.freeze({
    [COMPRESSION_JOB_STATUS.PENDING]: faClock,
    [COMPRESSION_JOB_STATUS.SUCCEEDED]: faCheck,
    [COMPRESSION_JOB_STATUS.FAILED]: faExclamation,
});

/**
 * Renders an ingestion job.
 *
 * @param {import("/imports/api/ingestion/types").CompressionJob} job The job object containing
 * information about the compression job.
 * @return {React.ReactElement}
 */
const IngestionJobRow = ({job}) => {
    let uncompressedSizeText = "";
    let compressedSizeText = "";
    let speedText = "";

    if (null === job.duration && null !== job.start_time) {
        job.duration = dayjs.duration(
            dayjs() - dayjs(job.start_time)
        ).asSeconds();
    }

    const uncompressedSize = Number(job.uncompressed_size);
    if (false === isNaN(uncompressedSize) && 0 !== uncompressedSize) {
        uncompressedSizeText = computeHumanSize(uncompressedSize);
    }

    const compressedSize = Number(job.compressed_size);
    if (false === isNaN(compressedSize) && 0 !== compressedSize) {
        compressedSizeText = computeHumanSize(compressedSize);
    }

    if (
        false === isNaN(uncompressedSize) &&
        0 !== uncompressedSize &&
        0 < job.duration
    ) {
        speedText = `${computeHumanSize(job.uncompressed_size / job.duration)}/s`;
    }

    const isPlaceholderVisible = job.status !== COMPRESSION_JOB_STATUS.FAILED;

    return (
        <tr>
            <td className={"text-center"}>
                <OverlayTrigger
                    placement={"bottom-start"}
                    overlay={
                        <Tooltip>
                            {COMPRESSION_JOB_STATUS_NAMES[job.status]}
                            {job.status_msg && (` - ${job.status_msg}`)}
                        </Tooltip>
                    }
                >
                    {COMPRESSION_JOB_STATUS.RUNNING === job.status ?
                        <Spinner size={"sm"}/> :
                        <FontAwesomeIcon
                            fixedWidth={true}
                            icon={COMPRESSION_JOB_STATUS_ICONS[job.status]}/>}
                </OverlayTrigger>
            </td>
            <td className={"fw-bold text-end"}>
                {job._id}
            </td>
            <td className={"text-end"}>
                <PlaceholderText
                    isAlwaysVisible={isPlaceholderVisible}
                    text={speedText}/>
            </td>
            <td className={"text-end"}>
                <PlaceholderText
                    isAlwaysVisible={isPlaceholderVisible}
                    text={uncompressedSizeText}/>
            </td>
            <td className={"text-end"}>
                <PlaceholderText
                    isAlwaysVisible={isPlaceholderVisible}
                    text={compressedSizeText}/>
            </td>
            <td className={"text-end"}>
                <PlaceholderText
                    isAlwaysVisible={isPlaceholderVisible}
                    text={(null === job.update_time) ?
                        "null" :
                        new Date(job.update_time).toLocaleString()}/>
            </td>
        </tr>
    );
};

export default IngestionJobRow;
