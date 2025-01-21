import {useTracker} from "meteor/react-meteor-data";
import Table from "react-bootstrap/Table";

import {faBarsProgress} from "@fortawesome/free-solid-svg-icons";

import {CompressionJobsCollection} from "/imports/api/ingestion/collections";
import {MONGO_SORT_BY_ID} from "/imports/utils/mongo";

import Panel from "../../Panel";
import IngestionJobRow from "./IngestionJobRow";

import "./IngestionJobs.scss";


/**
 * Displays a table of ingestion jobs.
 *
 * @return {React.ReactElement}
 */
const IngestionJobs = () => {
    const compressionJobs = useTracker(() => {
        Meteor.subscribe(Meteor.settings.public.CompressionJobsCollectionName);

        const findOptions = {
            sort: [MONGO_SORT_BY_ID],
        };

        return CompressionJobsCollection.find({}, findOptions).fetch();
    }, []);

    if (0 === compressionJobs.length) {
        return <></>;
    }

    return (
        <Panel
            faIcon={faBarsProgress}
            title={"Ingestion Jobs"}
            xl={6}
            xs={12}
        >
            <Table className={"ingestion-jobs-table"}>
                <thead>
                    <tr>
                        <th className={"text-center col-1"}>Status</th>
                        <th className={"text-end"}>Job ID</th>
                        <th className={"text-end"}>Speed</th>
                        <th className={"text-end"}>Data Ingested</th>
                        <th className={"text-end"}>Compressed Size</th>
                        <th className={"text-end"}>Last Updated</th>
                    </tr>
                </thead>
                <tbody>
                    {compressionJobs.map((job, i) => (
                        <IngestionJobRow
                            job={job}
                            key={i}/>
                    ))}
                </tbody>
            </Table>
        </Panel>
    );
};

export default IngestionJobs;
