import argparse
import datetime
import logging
import os
import sys
import time
from contextlib import closing
from pathlib import Path

import brotli
import celery
import msgpack
from clp_package_utils.general import CONTAINER_INPUT_LOGS_ROOT_DIR
from clp_py_utils.clp_config import (
    CLP_METADATA_TABLE_PREFIX,
    CLPConfig,
    COMPRESSION_JOBS_TABLE_NAME,
    COMPRESSION_TASKS_TABLE_NAME,
)
from clp_py_utils.clp_logging import get_logger, get_logging_formatter, set_logging_level
from clp_py_utils.compression import validate_path_and_get_info
from clp_py_utils.core import read_yaml_config_file
from clp_py_utils.s3_utils import s3_get_object_metadata
from clp_py_utils.sql_adapter import SQL_Adapter
from job_orchestration.executor.compress.compression_task import compress
from job_orchestration.scheduler.compress.partition import PathsToCompressBuffer
from job_orchestration.scheduler.constants import CompressionJobStatus, CompressionTaskStatus
from job_orchestration.scheduler.job_config import (
    ClpIoConfig,
    FsInputConfig,
    InputType,
    S3InputConfig,
)
from job_orchestration.scheduler.scheduler_data import (
    CompressionJob,
    CompressionTaskResult,
)
from pydantic import ValidationError

# Setup logging
logger = get_logger("compression_scheduler")

scheduled_jobs = {}


def fetch_new_jobs(db_cursor):
    db_cursor.execute(
        f"""
        SELECT id, clp_config, creation_time
        FROM {COMPRESSION_JOBS_TABLE_NAME}
        WHERE status='{CompressionJobStatus.PENDING}'
        """
    )
    return db_cursor.fetchall()


def update_compression_task_metadata(db_cursor, task_id, kv):
    if not len(kv):
        logger.error("Must specify at least one field to update")
        raise ValueError

    field_set_expressions = [f"{k} = %s" for k in kv.keys()]
    query = f"""
        UPDATE {COMPRESSION_TASKS_TABLE_NAME}
        SET {", ".join(field_set_expressions)}
        WHERE id = %s
    """
    values = list(kv.values()) + [task_id]
    db_cursor.execute(query, values)


def update_compression_job_metadata(db_cursor, job_id, kv):
    if not len(kv):
        logger.error("Must specify at least one field to update")
        raise ValueError

    field_set_expressions = [f"{k} = %s" for k in kv.keys()]
    query = f"""
        UPDATE {COMPRESSION_JOBS_TABLE_NAME}
        SET {", ".join(field_set_expressions)}
        WHERE id = %s
    """
    values = list(kv.values()) + [job_id]
    db_cursor.execute(query, values)


def _process_fs_input_paths(
    fs_input_conf: FsInputConfig, paths_to_compress_buffer: PathsToCompressBuffer
) -> None:
    """
    Iterates through all files in fs_input_conf and adds their metadata to
    `paths_to_compress_buffer`.
    NOTE: This method skips files that don't exist.
    :param fs_input_conf:
    :param paths_to_compress_buffer:
    """

    for path_idx, path in enumerate(fs_input_conf.paths_to_compress, start=1):
        path = Path(path)

        try:
            file, empty_directory = validate_path_and_get_info(CONTAINER_INPUT_LOGS_ROOT_DIR, path)
        except ValueError as ex:
            logger.error(str(ex))
            continue

        if file:
            paths_to_compress_buffer.add_file(file)
        elif empty_directory:
            paths_to_compress_buffer.add_empty_directory(empty_directory)

        if path.is_dir():
            for internal_path in path.rglob("*"):
                try:
                    file, empty_directory = validate_path_and_get_info(
                        CONTAINER_INPUT_LOGS_ROOT_DIR, internal_path
                    )
                except ValueError as ex:
                    logger.error(str(ex))
                    continue

                if file:
                    paths_to_compress_buffer.add_file(file)
                elif empty_directory:
                    paths_to_compress_buffer.add_empty_directory(empty_directory)


def _process_s3_input(
    s3_input_config: S3InputConfig,
    paths_to_compress_buffer: PathsToCompressBuffer,
) -> None:
    """
    Iterates through all objects under the <bucket>/<key_prefix> specified by s3_input_config,
    and adds their metadata to paths_to_compress_buffer.
    :param s3_input_config:
    :param paths_to_compress_buffer:
    :raises: RuntimeError if input URL doesn't resolve to any objects.
    :raises: Propagates `s3_get_object_metadata`'s exceptions.
    """

    object_metadata_list = s3_get_object_metadata(s3_input_config)
    if len(object_metadata_list) == 0:
        raise RuntimeError("Input URL doesn't resolve to any object")

    for object_metadata in object_metadata_list:
        paths_to_compress_buffer.add_file(object_metadata)


def search_and_schedule_new_tasks(db_conn, db_cursor, clp_metadata_db_connection_config):
    """
    For all jobs with PENDING status, split the job into tasks and schedule them.
    """
    global scheduled_jobs

    logger.debug("Search and schedule new tasks")

    # Poll for new compression jobs
    jobs = fetch_new_jobs(db_cursor)
    db_conn.commit()
    for job_row in jobs:
        job_id = job_row["id"]
        clp_io_config = ClpIoConfig.parse_obj(
            msgpack.unpackb(brotli.decompress(job_row["clp_config"]))
        )

        paths_to_compress_buffer = PathsToCompressBuffer(
            maintain_file_ordering=False,
            empty_directories_allowed=True,
            scheduling_job_id=job_id,
            clp_io_config=clp_io_config,
            clp_metadata_db_connection_config=clp_metadata_db_connection_config,
        )

        input_config = clp_io_config.input
        input_type = input_config.type
        if input_type == InputType.FS.value:
            _process_fs_input_paths(input_config, paths_to_compress_buffer)
        elif input_type == InputType.S3.value:
            try:
                _process_s3_input(input_config, paths_to_compress_buffer)
            except Exception as err:
                logger.exception("Failed to process S3 input")
                update_compression_job_metadata(
                    db_cursor,
                    job_id,
                    {
                        "status": CompressionJobStatus.FAILED,
                        "status_msg": f"S3 Failure: {err}",
                    },
                )
                db_conn.commit()
                continue
        else:
            logger.error(f"Unsupported input type {input_type}")
            update_compression_job_metadata(
                db_cursor,
                job_id,
                {
                    "status": CompressionJobStatus.FAILED,
                    "status_msg": f"Unsupported input type: {input_type}",
                },
            )
            db_conn.commit()
            continue

        paths_to_compress_buffer.flush()
        tasks = paths_to_compress_buffer.get_tasks()
        partition_info = paths_to_compress_buffer.get_partition_info()

        if len(tasks) == 0:
            logger.warning(f"No tasks were created for job {job_id}")
            update_time = datetime.datetime.now()
            update_compression_job_metadata(
                db_cursor,
                job_id,
                {
                    "status": CompressionJobStatus.FAILED,
                    "status_msg": "invalid input path",
                    "update_time": update_time,
                },
            )
            db_conn.commit()
            continue

        # Update job metadata
        start_time = datetime.datetime.now()
        update_compression_job_metadata(
            db_cursor,
            job_id,
            {
                "num_tasks": paths_to_compress_buffer.num_tasks,
                "status": CompressionJobStatus.RUNNING,
                "start_time": start_time,
                "update_time": start_time,
            },
        )
        db_conn.commit()

        tag_ids = None
        if clp_io_config.output.tags:
            db_cursor.executemany(
                f"INSERT IGNORE INTO {CLP_METADATA_TABLE_PREFIX}tags (tag_name) VALUES (%s)",
                [(tag,) for tag in clp_io_config.output.tags],
            )
            db_conn.commit()
            db_cursor.execute(
                f"SELECT tag_id FROM {CLP_METADATA_TABLE_PREFIX}tags WHERE tag_name IN (%s)"
                % ", ".join(["%s"] * len(clp_io_config.output.tags)),
                clp_io_config.output.tags,
            )
            tag_ids = [tags["tag_id"] for tags in db_cursor.fetchall()]
            db_conn.commit()

        task_instances = []
        for task_idx, task in enumerate(tasks):
            db_cursor.execute(
                f"""
                INSERT INTO {COMPRESSION_TASKS_TABLE_NAME}
                (job_id, partition_original_size, clp_paths_to_compress)
                VALUES({str(job_id)}, {partition_info[task_idx]["partition_original_size"]}, %s)
                """,
                (partition_info[task_idx]["clp_paths_to_compress"],),
            )
            db_conn.commit()
            task["task_id"] = db_cursor.lastrowid
            task["tag_ids"] = tag_ids
            task_instances.append(compress.s(**task))
        tasks_group = celery.group(task_instances)

        job = CompressionJob(
            id=job_id, start_time=start_time, async_task_result=tasks_group.apply_async()
        )
        db_cursor.execute(
            f"""
            UPDATE {COMPRESSION_TASKS_TABLE_NAME}
            SET status='{CompressionTaskStatus.RUNNING}' WHERE job_id={job_id}
            """
        )
        db_conn.commit()

        scheduled_jobs[job_id] = job


def get_results_or_timeout(result):
    try:
        return result.get(timeout=0.1)
    except celery.exceptions.TimeoutError:
        return None


def poll_running_jobs(db_conn, db_cursor):
    """
    Poll for running jobs and update their status.
    """
    global scheduled_jobs

    logger.debug("Poll running jobs")
    jobs_to_delete = []
    for job_id, job in scheduled_jobs.items():
        job_success = True
        duration = 0.0
        error_message = ""

        try:
            returned_results = get_results_or_timeout(job.async_task_result)
            if returned_results is None:
                continue

            duration = (datetime.datetime.now() - job.start_time).total_seconds()
            # Check for finished jobs
            for task_result in returned_results:
                task_result = CompressionTaskResult.parse_obj(task_result)
                if task_result.status == CompressionTaskStatus.SUCCEEDED:
                    logger.info(
                        f"Compression task job-{job_id}-task-{task_result.task_id} completed in"
                        f" {task_result.duration} second(s)."
                    )
                else:
                    job_success = False
                    error_message += f"task {task_result.task_id}: {task_result.error_message}\n"
                    logger.error(
                        f"Compression task job-{job_id}-task-{task_result.task_id} failed with"
                        f" error: {task_result.error_message}."
                    )

        except Exception as e:
            logger.error(f"Error while getting results for job {job_id}: {e}")
            job_success = False

        update_time = datetime.datetime.now()
        if job_success:
            logger.info(f"Job {job_id} succeeded.")
            update_compression_job_metadata(
                db_cursor,
                job_id,
                dict(
                    status=CompressionJobStatus.SUCCEEDED,
                    duration=duration,
                    update_time=update_time,
                ),
            )
        else:
            logger.error(f"Job {job_id} failed. See worker logs or status_msg for details.")
            update_compression_job_metadata(
                db_cursor,
                job_id,
                dict(
                    status=CompressionJobStatus.FAILED,
                    status_msg=error_message,
                    update_time=update_time,
                ),
            )
        db_conn.commit()

        jobs_to_delete.append(job_id)

    for job_id in jobs_to_delete:
        del scheduled_jobs[job_id]


def main(argv):
    args_parser = argparse.ArgumentParser()
    args_parser.add_argument("--config", "-c", required=True, help="CLP configuration file.")
    args = args_parser.parse_args(argv[1:])

    # Setup logging
    log_file = Path(os.getenv("CLP_LOGS_DIR")) / "compression_scheduler.log"
    logging_file_handler = logging.FileHandler(filename=log_file, encoding="utf-8")
    logging_file_handler.setFormatter(get_logging_formatter())
    logger.addHandler(logging_file_handler)

    # Update logging level based on config
    set_logging_level(logger, os.getenv("CLP_LOGGING_LEVEL"))

    # Load configuration
    config_path = Path(args.config)
    try:
        clp_config = CLPConfig.parse_obj(read_yaml_config_file(config_path))
    except ValidationError as err:
        logger.error(err)
        return -1
    except Exception as ex:
        logger.error(ex)
        # read_yaml_config_file already logs the parsing error inside
        return -1

    logger.info("Starting compression scheduler")
    sql_adapter = SQL_Adapter(clp_config.database)

    with closing(sql_adapter.create_connection(True)) as db_conn, closing(
        db_conn.cursor(dictionary=True)
    ) as db_cursor:
        # Start Job Processing Loop
        while True:
            try:
                search_and_schedule_new_tasks(
                    db_conn,
                    db_cursor,
                    sql_adapter.database_config.get_clp_connection_params_and_type(True),
                )
                poll_running_jobs(db_conn, db_cursor)
                time.sleep(clp_config.compression_scheduler.jobs_poll_delay)
            except KeyboardInterrupt:
                logger.info("Gracefully shutting down")
                return -1
            except Exception:
                logger.exception(f"Error in scheduling.")
                return -1


if "__main__" == __name__:
    sys.exit(main(sys.argv))
