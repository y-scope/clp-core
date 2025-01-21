#!/usr/bin/env python3
import argparse
import logging
import sys
from contextlib import closing

from job_orchestration.scheduler.constants import (
    CompressionJobStatus,
    CompressionTaskStatus,
    QueryJobStatus,
    QueryTaskStatus,
)
from sql_adapter import SQL_Adapter

from clp_py_utils.clp_config import (
    COMPRESSION_JOBS_TABLE_NAME,
    COMPRESSION_TASKS_TABLE_NAME,
    Database,
    QUERY_JOBS_TABLE_NAME,
    QUERY_TASKS_TABLE_NAME,
)
from clp_py_utils.core import read_yaml_config_file

# Setup logging
# Create logger
logger = logging.getLogger(__file__)
logger.setLevel(logging.INFO)
# Setup console logging
logging_console_handler = logging.StreamHandler()
logging_formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s")
logging_console_handler.setFormatter(logging_formatter)
logger.addHandler(logging_console_handler)


def main(argv):
    args_parser = argparse.ArgumentParser(
        description="Sets up metadata tables for job orchestration."
    )
    args_parser.add_argument("--config", required=True, help="Database config file.")
    parsed_args = args_parser.parse_args(argv[1:])

    try:
        database_config = Database.parse_obj(read_yaml_config_file(parsed_args.config))
        if database_config is None:
            raise ValueError(f"Database configuration file '{parsed_args.config}' is empty.")
        sql_adapter = SQL_Adapter(database_config)
        with closing(sql_adapter.create_connection(True)) as scheduling_db, closing(
            scheduling_db.cursor(dictionary=True)
        ) as scheduling_db_cursor:
            scheduling_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{COMPRESSION_JOBS_TABLE_NAME}` (
                    `id` INT NOT NULL AUTO_INCREMENT,
                    `status` INT NOT NULL DEFAULT '{CompressionJobStatus.PENDING}',
                    `status_msg` VARCHAR(512) NOT NULL DEFAULT '',
                    `creation_time` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
                    `start_time` DATETIME(3) NULL DEFAULT NULL,
                    `update_time` DATETIME(3) NULL DEFAULT NULL,
                    `duration` FLOAT NULL DEFAULT NULL,
                    `original_size` BIGINT NOT NULL DEFAULT '0',
                    `uncompressed_size` BIGINT NOT NULL DEFAULT '0',
                    `compressed_size` BIGINT NOT NULL DEFAULT '0',
                    `num_tasks` INT NOT NULL DEFAULT '0',
                    `num_tasks_completed` INT NOT NULL DEFAULT '0',
                    `clp_binary_version` INT NULL DEFAULT NULL,
                    `clp_config` VARBINARY(60000) NOT NULL,
                    PRIMARY KEY (`id`) USING BTREE,
                    INDEX `JOB_STATUS` (`status`) USING BTREE
                ) ROW_FORMAT=DYNAMIC
                """
            )

            scheduling_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{COMPRESSION_TASKS_TABLE_NAME}` (
                    `id` BIGINT NOT NULL AUTO_INCREMENT,
                    `status` INT NOT NULL DEFAULT '{CompressionTaskStatus.PENDING}',
                    `creation_time` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
                    `start_time` DATETIME(3) NULL DEFAULT NULL,
                    `duration` FLOAT NULL DEFAULT NULL,
                    `job_id` INT NOT NULL,
                    `clp_paths_to_compress` VARBINARY(60000) NOT NULL,
                    `partition_original_size` BIGINT NOT NULL,
                    `partition_uncompressed_size` BIGINT NULL DEFAULT NULL,
                    `partition_compressed_size` BIGINT NULL DEFAULT NULL,
                    PRIMARY KEY (`id`) USING BTREE,
                    INDEX `job_id` (`job_id`) USING BTREE,
                    INDEX `TASK_STATUS` (`status`) USING BTREE,
                    INDEX `TASK_START_TIME` (`start_time`) USING BTREE,
                    CONSTRAINT `{COMPRESSION_TASKS_TABLE_NAME}` FOREIGN KEY (`job_id`) 
                    REFERENCES `{COMPRESSION_JOBS_TABLE_NAME}` (`id`)
                    ON UPDATE NO ACTION ON DELETE NO ACTION
                ) ROW_FORMAT=DYNAMIC
                """
            )

            scheduling_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{QUERY_JOBS_TABLE_NAME}` (
                    `id` INT NOT NULL AUTO_INCREMENT,
                    `type` INT NOT NULL,
                    `status` INT NOT NULL DEFAULT '{QueryJobStatus.PENDING}',
                    `creation_time` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
                    `num_tasks` INT NOT NULL DEFAULT '0',
                    `num_tasks_completed` INT NOT NULL DEFAULT '0',
                    `start_time` DATETIME(3) NULL DEFAULT NULL,
                    `duration` FLOAT NULL DEFAULT NULL,
                    `job_config` VARBINARY(60000) NOT NULL,
                    PRIMARY KEY (`id`) USING BTREE,
                    INDEX `JOB_STATUS` (`status`) USING BTREE
                ) ROW_FORMAT=DYNAMIC
                """
            )

            scheduling_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{QUERY_TASKS_TABLE_NAME}` (
                    `id` BIGINT NOT NULL AUTO_INCREMENT,
                    `status` INT NOT NULL DEFAULT '{QueryTaskStatus.PENDING}',
                    `creation_time` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
                    `start_time` DATETIME(3) NULL DEFAULT NULL,
                    `duration` FLOAT NULL DEFAULT NULL,
                    `job_id` INT NOT NULL,
                    `archive_id` VARCHAR(255) NULL DEFAULT NULL,
                    PRIMARY KEY (`id`) USING BTREE,
                    INDEX `job_id` (`job_id`) USING BTREE,
                    INDEX `TASK_STATUS` (`status`) USING BTREE,
                    INDEX `TASK_START_TIME` (`start_time`) USING BTREE,
                    CONSTRAINT `{QUERY_TASKS_TABLE_NAME}` FOREIGN KEY (`job_id`) 
                    REFERENCES `{QUERY_JOBS_TABLE_NAME}` (`id`)
                    ON UPDATE NO ACTION ON DELETE NO ACTION
                ) ROW_FORMAT=DYNAMIC
                """
            )

            scheduling_db.commit()
    except:
        logger.exception("Failed to create scheduling tables.")
        return -1

    return 0


if "__main__" == __name__:
    sys.exit(main(sys.argv))
