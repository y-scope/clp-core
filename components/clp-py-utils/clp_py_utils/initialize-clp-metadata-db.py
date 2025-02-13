#!/usr/bin/env python3
import argparse
import logging
import sys
from contextlib import closing

from sql_adapter import SQL_Adapter

from clp_py_utils.clp_config import Database
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
    args_parser = argparse.ArgumentParser(description="Sets up CLP's metadata tables.")
    args_parser.add_argument("--config", required=True, help="Database config file.")
    parsed_args = args_parser.parse_args(argv[1:])

    try:
        database_config = Database.parse_obj(read_yaml_config_file(parsed_args.config))
        if database_config is None:
            raise ValueError(f"Database configuration file '{parsed_args.config}' is empty.")
        sql_adapter = SQL_Adapter(database_config)
        clp_db_connection_params = database_config.get_clp_connection_params_and_type(True)
        table_prefix = clp_db_connection_params["table_prefix"]
        with closing(sql_adapter.create_connection(True)) as metadata_db, closing(
            metadata_db.cursor(dictionary=True)
        ) as metadata_db_cursor:
            metadata_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{table_prefix}archives` (
                    `pagination_id` BIGINT unsigned NOT NULL AUTO_INCREMENT,
                    `id` VARCHAR(64) NOT NULL,
                    `begin_timestamp` BIGINT NOT NULL,
                    `end_timestamp` BIGINT NOT NULL,
                    `uncompressed_size` BIGINT NOT NULL,
                    `size` BIGINT NOT NULL,
                    `creator_id` VARCHAR(64) NOT NULL,
                    `creation_ix` INT NOT NULL,
                    KEY `archives_creation_order` (`creator_id`,`creation_ix`) USING BTREE,
                    UNIQUE KEY `archive_id` (`id`) USING BTREE,
                    PRIMARY KEY (`pagination_id`)
                )
                """
            )

            metadata_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{table_prefix}tags` (
                    `tag_id` INT unsigned NOT NULL AUTO_INCREMENT,
                    `tag_name` VARCHAR(255) NOT NULL,
                    UNIQUE KEY (`tag_name`) USING BTREE,
                    PRIMARY KEY (`tag_id`)
                )
                """
            )

            metadata_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{table_prefix}archive_tags` (
                    `archive_id` VARCHAR(64) NOT NULL,
                    `tag_id` INT unsigned NOT NULL,
                    PRIMARY KEY (`archive_id`,`tag_id`),
                    FOREIGN KEY (`archive_id`) REFERENCES `{table_prefix}archives` (`id`),
                    FOREIGN KEY (`tag_id`) REFERENCES `{table_prefix}tags` (`tag_id`)
                )
                """
            )

            metadata_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{table_prefix}files` (
                    `id` VARCHAR(64) NOT NULL,
                    `orig_file_id` VARCHAR(64) NOT NULL,
                    `path` VARCHAR(12288) NOT NULL,
                    `begin_timestamp` BIGINT NOT NULL,
                    `end_timestamp` BIGINT NOT NULL,
                    `num_uncompressed_bytes` BIGINT NOT NULL,
                    `begin_message_ix` BIGINT NOT NULL,
                    `num_messages` BIGINT NOT NULL,
                    `archive_id` VARCHAR(64) NOT NULL,
                    KEY `files_path` (path(768)) USING BTREE,
                    KEY `files_archive_id` (`archive_id`) USING BTREE,
                    PRIMARY KEY (`id`)
                ) ROW_FORMAT=DYNAMIC
                """
            )

            metadata_db_cursor.execute(
                f"""
                CREATE TABLE IF NOT EXISTS `{table_prefix}column_metadata_default` (
                    `name` VARCHAR(512) NOT NULL,
                    `type` BIGINT NOT NULL,
                    PRIMARY KEY (`name`, `type`)
                )
                """
            )

            metadata_db.commit()
    except:
        logger.exception("Failed to create clp metadata tables.")
        return -1

    return 0


if "__main__" == __name__:
    sys.exit(main(sys.argv))
