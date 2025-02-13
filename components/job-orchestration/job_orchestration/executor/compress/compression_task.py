import datetime
import json
import os
import pathlib
import subprocess
from contextlib import closing
from typing import Any, Dict, List, Optional, Tuple

import yaml
from celery.app.task import Task
from celery.utils.log import get_task_logger
from clp_py_utils.clp_config import (
    COMPRESSION_JOBS_TABLE_NAME,
    COMPRESSION_TASKS_TABLE_NAME,
    Database,
    S3Config,
    StorageEngine,
    StorageType,
    WorkerConfig,
)
from clp_py_utils.clp_logging import set_logging_level
from clp_py_utils.core import read_yaml_config_file
from clp_py_utils.s3_utils import generate_s3_virtual_hosted_style_url, s3_put
from clp_py_utils.sql_adapter import SQL_Adapter
from job_orchestration.executor.compress.celery import app
from job_orchestration.scheduler.constants import CompressionTaskStatus
from job_orchestration.scheduler.job_config import (
    ClpIoConfig,
    InputType,
    PathsToCompress,
    S3InputConfig,
)
from job_orchestration.scheduler.scheduler_data import CompressionTaskResult

# Setup logging
logger = get_task_logger(__name__)


def update_compression_task_metadata(db_cursor, task_id, kv):
    if not len(kv):
        raise ValueError("Must specify at least one field to update")

    field_set_expressions = [f'{k}="{v}"' for k, v in kv.items()]
    query = f"""
    UPDATE {COMPRESSION_TASKS_TABLE_NAME}
    SET {", ".join(field_set_expressions)}
    WHERE id={task_id}
    """
    db_cursor.execute(query)


def increment_compression_job_metadata(db_cursor, job_id, kv):
    if not len(kv):
        raise ValueError("Must specify at least one field to update")

    field_set_expressions = [f"{k}={k}+{v}" for k, v in kv.items()]
    query = f"""
    UPDATE {COMPRESSION_JOBS_TABLE_NAME}
    SET {", ".join(field_set_expressions)}
    WHERE id={job_id}
    """
    db_cursor.execute(query)


def update_tags(db_cursor, table_prefix, archive_id, tag_ids):
    db_cursor.executemany(
        f"INSERT INTO {table_prefix}archive_tags (archive_id, tag_id) VALUES (%s, %s)",
        [(archive_id, tag_id) for tag_id in tag_ids],
    )


def update_job_metadata_and_tags(db_cursor, job_id, table_prefix, tag_ids, archive_stats):
    if tag_ids is not None:
        update_tags(db_cursor, table_prefix, archive_stats["id"], tag_ids)
    increment_compression_job_metadata(
        db_cursor,
        job_id,
        dict(
            uncompressed_size=archive_stats["uncompressed_size"],
            compressed_size=archive_stats["size"],
        ),
    )


def _generate_fs_logs_list(
    output_file_path: pathlib.Path,
    paths_to_compress: PathsToCompress,
) -> None:
    file_paths = paths_to_compress.file_paths
    empty_directories = paths_to_compress.empty_directories
    with open(output_file_path, "w") as file:
        for path_str in file_paths:
            file.write(path_str)
            file.write("\n")
        if empty_directories and len(empty_directories) > 0:
            # Prepare list of paths to compress for clp
            for path_str in empty_directories:
                file.write(path_str)
                file.write("\n")


def _generate_s3_logs_list(
    output_file_path: pathlib.Path,
    paths_to_compress: PathsToCompress,
    s3_input_config: S3InputConfig,
) -> None:
    # S3 object keys are stored as file_paths in `PathsToCompress`
    object_keys = paths_to_compress.file_paths
    with open(output_file_path, "w") as file:
        for object_key in object_keys:
            s3_virtual_hosted_style_url = generate_s3_virtual_hosted_style_url(
                s3_input_config.region_code, s3_input_config.bucket, object_key
            )
            file.write(s3_virtual_hosted_style_url)
            file.write("\n")


def make_clp_command_and_env(
    clp_home: pathlib.Path,
    archive_output_dir: pathlib.Path,
    clp_config: ClpIoConfig,
    db_config_file_path: pathlib.Path,
) -> Tuple[List[str], Optional[Dict[str, str]]]:
    """
    Generates the command and environment variables for a clp compression job.
    :param clp_home:
    :param archive_output_dir:
    :param clp_config:
    :param db_config_file_path:
    :return: Tuple of (compression_command, compression_env_vars)
    """

    path_prefix_to_remove = clp_config.input.path_prefix_to_remove

    # fmt: off
    compression_cmd = [
        str(clp_home / "bin" / "clp"),
        "c", str(archive_output_dir),
        "--print-archive-stats-progress",
        "--target-dictionaries-size", str(clp_config.output.target_dictionaries_size),
        "--target-segment-size", str(clp_config.output.target_segment_size),
        "--target-encoded-file-size", str(clp_config.output.target_encoded_file_size),
        "--db-config-file", str(db_config_file_path),
    ]
    # fmt: on
    if path_prefix_to_remove:
        compression_cmd.append("--remove-path-prefix")
        compression_cmd.append(path_prefix_to_remove)

    # Use schema file if it exists
    schema_path: pathlib.Path = clp_home / "etc" / "clp-schema.txt"
    if schema_path.exists():
        compression_cmd.append("--schema-path")
        compression_cmd.append(str(schema_path))

    return compression_cmd, None


def make_clp_s_command_and_env(
    clp_home: pathlib.Path,
    archive_output_dir: pathlib.Path,
    clp_config: ClpIoConfig,
    db_config_file_path: pathlib.Path,
    use_single_file_archive: bool,
) -> Tuple[List[str], Optional[Dict[str, str]]]:
    """
    Generates the command and environment variables for a clp_s compression job.
    :param clp_home:
    :param archive_output_dir:
    :param clp_config:
    :param db_config_file_path:
    :param use_single_file_archive:
    :return: Tuple of (compression_command, compression_env_vars)
    """

    # fmt: off
    compression_cmd = [
        str(clp_home / "bin" / "clp-s"),
        "c", str(archive_output_dir),
        "--print-archive-stats",
        "--target-encoded-size",
        str(clp_config.output.target_segment_size + clp_config.output.target_dictionaries_size),
        "--db-config-file", str(db_config_file_path),
    ]
    # fmt: on

    if InputType.S3 == clp_config.input.type:
        compression_env_vars = {
            **os.environ,
            "AWS_ACCESS_KEY_ID": clp_config.input.credentials.access_key_id,
            "AWS_SECRET_ACCESS_KEY": clp_config.input.credentials.secret_access_key,
        }
        compression_cmd.append("--auth")
        compression_cmd.append("s3")
    else:
        compression_env_vars = None

    if use_single_file_archive:
        compression_cmd.append("--single-file-archive")

    if clp_config.input.timestamp_key is not None:
        compression_cmd.append("--timestamp-key")
        compression_cmd.append(clp_config.input.timestamp_key)

    return compression_cmd, compression_env_vars


def run_clp(
    worker_config: WorkerConfig,
    clp_config: ClpIoConfig,
    clp_home: pathlib.Path,
    logs_dir: pathlib.Path,
    job_id: int,
    task_id: int,
    tag_ids,
    paths_to_compress: PathsToCompress,
    sql_adapter: SQL_Adapter,
    clp_metadata_db_connection_config,
):
    """
    Compresses logs into archives.

    :param worker_config: WorkerConfig
    :param clp_config: ClpIoConfig
    :param clp_home:
    :param logs_dir:
    :param job_id:
    :param task_id:
    :param tag_ids:
    :param paths_to_compress: PathToCompress
    :param sql_adapter: SQL_Adapter
    :param clp_metadata_db_connection_config
    :return: tuple -- (whether compression was successful, output messages)
    """
    instance_id_str = f"compression-job-{job_id}-task-{task_id}"

    clp_storage_engine = worker_config.package.storage_engine
    data_dir = worker_config.data_directory
    archive_output_dir = worker_config.archive_output.get_directory()

    # Generate database config file for clp
    db_config_file_path = data_dir / f"{instance_id_str}-db-config.yml"
    db_config_file = open(db_config_file_path, "w")
    yaml.safe_dump(clp_metadata_db_connection_config, db_config_file)
    db_config_file.close()

    # Get S3 config
    s3_config: S3Config
    enable_s3_write = False
    storage_type = worker_config.archive_output.storage.type
    if StorageType.S3 == storage_type:
        if StorageEngine.CLP_S != clp_storage_engine:
            error_msg = f"S3 storage is not supported for storage engine: {clp_storage_engine}."
            logger.error(error_msg)
            return False, {"error_message": error_msg}

        s3_config = worker_config.archive_output.storage.s3_config
        enable_s3_write = True

    if StorageEngine.CLP == clp_storage_engine:
        compression_cmd, compression_env = make_clp_command_and_env(
            clp_home=clp_home,
            archive_output_dir=archive_output_dir,
            clp_config=clp_config,
            db_config_file_path=db_config_file_path,
        )
    elif StorageEngine.CLP_S == clp_storage_engine:
        compression_cmd, compression_env = make_clp_s_command_and_env(
            clp_home=clp_home,
            archive_output_dir=archive_output_dir,
            clp_config=clp_config,
            db_config_file_path=db_config_file_path,
            use_single_file_archive=enable_s3_write,
        )
    else:
        logger.error(f"Unsupported storage engine {clp_storage_engine}")
        return False, {"error_message": f"Unsupported storage engine {clp_storage_engine}"}

    # Generate list of logs to compress
    input_type = clp_config.input.type
    logs_list_path = data_dir / f"{instance_id_str}-log-paths.txt"
    if InputType.FS == input_type:
        _generate_fs_logs_list(logs_list_path, paths_to_compress)
    elif InputType.S3 == input_type:
        _generate_s3_logs_list(logs_list_path, paths_to_compress, clp_config.input)
    else:
        error_msg = f"Unsupported input type: {input_type}."
        logger.error(error_msg)
        return False, {"error_message": error_msg}

    compression_cmd.append("--files-from")
    compression_cmd.append(str(logs_list_path))

    # Open stderr log file
    stderr_log_path = logs_dir / f"{instance_id_str}-stderr.log"
    stderr_log_file = open(stderr_log_path, "w")

    # Start compression
    logger.debug("Compressing...")
    compression_successful = False
    proc = subprocess.Popen(
        compression_cmd, stdout=subprocess.PIPE, stderr=stderr_log_file, env=compression_env
    )

    # Compute the total amount of data compressed
    last_archive_stats = None
    last_line_decoded = False
    total_uncompressed_size = 0
    total_compressed_size = 0

    # Handle job metadata update and S3 write if enabled
    s3_error = None
    while not last_line_decoded:
        stats: Optional[Dict[str, Any]] = None

        line = proc.stdout.readline()
        if not line:
            last_line_decoded = True
        else:
            stats = json.loads(line.decode("ascii"))

        if last_archive_stats is not None and (
            None is stats or stats["id"] != last_archive_stats["id"]
        ):
            archive_id = last_archive_stats["id"]
            archive_path = archive_output_dir / archive_id
            if StorageEngine.CLP_S == clp_storage_engine:
                metadata_uploader_cmd = [
                    str(clp_home / "bin" / "indexer"),
                    "--db-config-file",
                    str(db_config_file_path),
                    "default",  # hardcode the table name for now
                    archive_path,
                ]
                proc = subprocess.Popen(
                    metadata_uploader_cmd, stdout=subprocess.DEVNULL, stderr=stderr_log_file
                )
                proc.wait()
            if enable_s3_write:
                if s3_error is None:
                    logger.info(f"Uploading archive {archive_id} to S3...")
                    try:
                        s3_put(s3_config, archive_path, archive_id)
                        logger.info(f"Finished uploading archive {archive_id} to S3.")
                    except Exception as err:
                        logger.exception(f"Failed to upload archive {archive_id}")
                        s3_error = str(err)
                        # NOTE: It's possible `proc` finishes before we call `terminate` on it, in
                        # which case the process will still return success.
                        proc.terminate()

                archive_path.unlink()

            if s3_error is None:
                # We've started a new archive so add the previous archive's last reported size to
                # the total
                total_uncompressed_size += last_archive_stats["uncompressed_size"]
                total_compressed_size += last_archive_stats["size"]
                with closing(sql_adapter.create_connection(True)) as db_conn, closing(
                    db_conn.cursor(dictionary=True)
                ) as db_cursor:
                    update_job_metadata_and_tags(
                        db_cursor,
                        job_id,
                        clp_metadata_db_connection_config["table_prefix"],
                        tag_ids,
                        last_archive_stats,
                    )
                    db_conn.commit()

        last_archive_stats = stats

    # Wait for compression to finish
    return_code = proc.wait()
    if 0 != return_code:
        logger.error(f"Failed to compress, return_code={str(return_code)}")
    else:
        compression_successful = True

        # Remove generated temporary files
        if logs_list_path:
            logs_list_path.unlink()
        db_config_file_path.unlink()
    logger.debug("Compressed.")

    # Close stderr log file
    stderr_log_file.close()

    worker_output = {
        "total_uncompressed_size": total_uncompressed_size,
        "total_compressed_size": total_compressed_size,
    }

    if compression_successful and s3_error is None:
        return CompressionTaskStatus.SUCCEEDED, worker_output
    else:
        error_msgs = []
        if compression_successful is False:
            error_msgs.append(f"See logs {stderr_log_path}")
        if s3_error is not None:
            error_msgs.append(s3_error)
        worker_output["error_message"] = "\n".join(error_msgs)
        return CompressionTaskStatus.FAILED, worker_output


@app.task(bind=True)
def compress(
    self: Task,
    job_id: int,
    task_id: int,
    tag_ids,
    clp_io_config_json: str,
    paths_to_compress_json: str,
    clp_metadata_db_connection_config,
):
    clp_home = pathlib.Path(os.getenv("CLP_HOME"))

    # Set logging level
    logs_dir = pathlib.Path(os.getenv("CLP_LOGS_DIR"))
    clp_logging_level = str(os.getenv("CLP_LOGGING_LEVEL"))
    set_logging_level(logger, clp_logging_level)

    # Load configuration
    try:
        worker_config = WorkerConfig.parse_obj(
            read_yaml_config_file(pathlib.Path(os.getenv("CLP_CONFIG_PATH")))
        )
    except Exception as ex:
        error_msg = "Failed to load worker config"
        logger.exception(error_msg)
        return CompressionTaskResult(
            task_id=task_id,
            status=CompressionTaskStatus.FAILED,
            duration=0,
            error_message=error_msg,
        )

    clp_io_config = ClpIoConfig.parse_raw(clp_io_config_json)
    paths_to_compress = PathsToCompress.parse_raw(paths_to_compress_json)

    sql_adapter = SQL_Adapter(Database.parse_obj(clp_metadata_db_connection_config))

    start_time = datetime.datetime.now()
    logger.info(f"[job_id={job_id} task_id={task_id}] COMPRESSION STARTED.")
    compression_task_status, worker_output = run_clp(
        worker_config,
        clp_io_config,
        clp_home,
        logs_dir,
        job_id,
        task_id,
        tag_ids,
        paths_to_compress,
        sql_adapter,
        clp_metadata_db_connection_config,
    )
    duration = (datetime.datetime.now() - start_time).total_seconds()
    logger.info(f"[job_id={job_id} task_id={task_id}] COMPRESSION COMPLETED.")

    with closing(sql_adapter.create_connection(True)) as db_conn, closing(
        db_conn.cursor(dictionary=True)
    ) as db_cursor:
        update_compression_task_metadata(
            db_cursor,
            task_id,
            dict(
                start_time=start_time,
                status=compression_task_status,
                partition_uncompressed_size=worker_output["total_uncompressed_size"],
                partition_compressed_size=worker_output["total_compressed_size"],
                duration=duration,
            ),
        )
        if CompressionTaskStatus.SUCCEEDED == compression_task_status:
            increment_compression_job_metadata(db_cursor, job_id, dict(num_tasks_completed=1))
        db_conn.commit()

    compression_task_result = CompressionTaskResult(
        task_id=task_id,
        status=compression_task_status,
        duration=duration,
    )

    if CompressionTaskStatus.FAILED == compression_task_status:
        compression_task_result.error_message = worker_output["error_message"]

    return compression_task_result.dict()
