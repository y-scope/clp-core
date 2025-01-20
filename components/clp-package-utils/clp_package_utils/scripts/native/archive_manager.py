import argparse
import logging
import shutil
import sys
from contextlib import closing
from pathlib import Path
from typing import List

from clp_py_utils.clp_config import Database
from clp_py_utils.sql_adapter import SQL_Adapter

from clp_package_utils.general import (
    CLP_DEFAULT_CONFIG_FILE_RELATIVE_PATH,
    get_clp_home,
    load_config_file,
)

# Command/Argument Constants
FIND_COMMAND = "find"
DEL_COMMAND = "del"
BY_IDS_COMMAND = "by-ids"
BY_FILTER_COMMAND = "by-filter"
BEGIN_TS_ARG = "--begin-ts"
END_TS_ARG = "--end-ts"
DRY_RUN_ARG = "--dry-run"

logger = logging.getLogger(__file__)


def main(argv):
    clp_home = get_clp_home()
    default_config_file_path = clp_home / CLP_DEFAULT_CONFIG_FILE_RELATIVE_PATH

    # Top-level parser and options
    args_parser = argparse.ArgumentParser(description="View or delete archives.")
    args_parser.add_argument(
        "--config",
        "-c",
        default=str(default_config_file_path),
        help="CLP configuration file.",
    )

    # Top-level commands
    subparsers = args_parser.add_subparsers(
        dest="subcommand",
        required=True,
    )
    find_parser = subparsers.add_parser(
        FIND_COMMAND,
        help="List IDs of archives.",
    )
    del_parser = subparsers.add_parser(
        DEL_COMMAND,
        help="Delete archives.",
    )

    # Find options
    find_parser.add_argument(
        BEGIN_TS_ARG,
        dest="begin_ts",
        type=int,
        help="Time-range lower-bound (inclusive) as milliseconds from the UNIX epoch.",
    )
    find_parser.add_argument(
        END_TS_ARG,
        dest="end_ts",
        type=int,
        help="Time-range upper-bound (include) as milliseconds from the UNIX epoch.",
    )

    # Delete options
    del_parser.add_argument(
        DRY_RUN_ARG,
        dest="dry_run",
        action="store_true",
        help="Preview delete without making changes. Lists errors and files to be deleted.",
    )

    # Delete subcommands
    del_subparsers = del_parser.add_subparsers(
        dest="del_subcommand",
        required=True,
    )
    del_id_parser = del_subparsers.add_parser(
        BY_IDS_COMMAND,
        help="Delete archives by ID.",
    )
    del_filter_parser = del_subparsers.add_parser(
        BY_FILTER_COMMAND,
        help="delete archives within time frame.",
    )

    # Delete by ID arguments
    del_id_parser.add_argument(
        "ids",
        nargs="+",
        help="List of archive IDs to delete",
    )

    # Delete by filter arguments
    del_filter_parser.add_argument(
        "begin_ts",
        type=int,
        help="Time-range lower-bound (inclusive) as milliseconds from the UNIX epoch.",
    )
    del_filter_parser.add_argument(
        "end_ts",
        type=int,
        help="Time-range upper-bound (include) as milliseconds from the UNIX epoch.",
    )

    parsed_args = args_parser.parse_args(argv[1:])

    # Validate and load config file
    config_file_path = Path(parsed_args.config)
    try:
        clp_config = load_config_file(config_file_path, default_config_file_path, clp_home)
        clp_config.validate_logs_dir()
    except:
        logger.exception("Failed to load config.")
        return -1

    database_config = clp_config.database
    archives_dir = clp_config.archive_output.get_directory()
    if not archives_dir.exists():
        logger.error("`archive_output.directory` doesn't exist.")
        return -1

    if FIND_COMMAND == parsed_args.subcommand:
        return _find_archives(
            archives_dir,
            database_config,
            parsed_args.begin_ts,
            parsed_args.end_ts,
        )
    elif DEL_COMMAND == parsed_args.subcommand:
        if BY_IDS_COMMAND == parsed_args.del_subcommand:
            return _delete_archives_by_ids(
                archives_dir,
                database_config,
                parsed_args.ids,
                parsed_args.dry_run,
            )
        elif BY_FILTER_COMMAND == parsed_args.del_subcommand:
            return _delete_archives_by_filter(
                archives_dir,
                database_config,
                parsed_args.begin_ts,
                parsed_args.end_ts,
                parsed_args.dry_run,
            )


def _find_archives(
    archives_dir: Path,
    database_config: Database,
    begin_ts: int,
    end_ts: int = None,
) -> int:
    """
    Lists all archive IDs, if begin_its and end_its are provided,
    only lists archives where `begin_ts <= archive.begin_timestamp` and
    `archive.end_timestamp <= end_ts`.
    :param archives_dir:
    :param database_config:
    :param begin_ts:
    :param end_ts:
    """
    archive_ids: List[str]
    logger.info("Starting to find archives from the database.")
    try:
        sql_adapter = SQL_Adapter(database_config)
        clp_db_connection_params = database_config.get_clp_connection_params_and_type(True)
        table_prefix = clp_db_connection_params["table_prefix"]
        with closing(sql_adapter.create_connection(True)) as db_conn, closing(
            db_conn.cursor(dictionary=True)
        ) as db_cursor:

            params = (begin_ts,)
            query = f"SELECT id FROM `{table_prefix}archives` WHERE begin_timestamp >= %s"
            if end_ts is not None:
                query += " AND end_timestamp <= %s"
                params = (begin_ts, end_ts)

            db_cursor.execute(query, params)
            results = db_cursor.fetchall()

            archive_ids = [result["id"] for result in results]

            if 0 == len(archive_ids):
                logger.info("No archives found within specified time range.")
                return 0

            logger.info(f"Found {len(archive_ids)} archives within specified time range.")
            for archive_id in archive_ids:
                logger.info(archive_id)

    except Exception:
        logger.exception("Failed to find archives from the database.")
        return -1

    logger.info(f"Finished finding archives from the database.")

    return 0


def _delete_archives(
    archives_dir: Path,
    database_config: Database,
    query: str,
    params: List[str],
    criteria: str,
    dry_run: bool = False,
) -> int:
    """
    Deletes archives from both metadata database and disk based on provided SQL query.
    :param archives_dir:
    :param database_config:
    :param query: SQL query to select archives to delete.
    :param params: List of parameters for SQL query.
    :param criteria: Type of deletion criteria. Either "filter" or "ids".
    :param dry_run: If True, no changes will be made to the database or disk.
    :return: 0 on success, -1 otherwise.
    """

    archive_ids: List[str]
    logger.info("Starting to delete archives from the database.")
    try:
        sql_adapter = SQL_Adapter(database_config)
        clp_db_connection_params = database_config.get_clp_connection_params_and_type(True)
        table_prefix = clp_db_connection_params["table_prefix"]
        with closing(sql_adapter.create_connection(True)) as db_conn, closing(
            db_conn.cursor(dictionary=True)
        ) as db_cursor:

            if dry_run:
                logger.info("Running in dry-run mode. No changes will be made to the database.")

            db_cursor.execute(query, params)
            results = db_cursor.fetchall()

            if 0 == len(results):
                if "filter" == criteria:
                    logger.info("No archives found within specified time frame.")
                elif "ids" == criteria:
                    logger.info("No archives found with matching IDs.")
                return 0

            archive_ids = [result["id"] for result in results]

            if "ids" == criteria:
                not_found_ids = set(params) - set(archive_ids)
                if not_found_ids:
                    logger.warning(
                        f"""
                        Failed to find archives with the following IDs:
                        {', '.join(not_found_ids)}
                        """
                    )

            id_string = ", ".join(f"'{archive_id}'" for archive_id in archive_ids)

            db_cursor.execute(
                f"""
                DELETE FROM `{table_prefix}files`
                WHERE archive_id in ({id_string})
                """
            )

            db_cursor.execute(
                f"""
                DELETE FROM `{table_prefix}archive_tags`
                WHERE archive_id in ({id_string})
                """
            )

            if dry_run:
                logger.info("Dry-run finished.")
                db_conn.rollback()
                return 0

            db_conn.commit()
            for archive_id in archive_ids:
                logger.info(f"Deleted archive {archive_id} from the database.")

    except Exception:
        logger.exception("Failed to delete archives from the database. Aborting deletion.")
        return -1

    logger.info(f"Finished deleting archives from the database.")

    for archive_id in archive_ids:
        archive_path = archives_dir / archive_id
        if not archive_path.is_dir():
            logger.warning(f"Archive {archive_id} is not a directory. Skipping deletion.")
            continue

        logger.info(f"Deleting archive {archive_id} from disk.")
        shutil.rmtree(archive_path)

    logger.info(f"Finished deleting archives from disk.")

    return 0


def _delete_archives_by_filter(
    archives_dir: Path,
    database_config: Database,
    begin_ts: int,
    end_ts: int,
    dry_run: bool = False,
) -> int:
    """
    Deletes all archives where `begin_ts <= archive.begin_timestamp` and
    `archive.end_timestamp <= end_ts` from both the metadata database and disk.
    :param archives_dir:
    :param database_config:
    :param begin_ts:
    :param end_ts:
    :param dry_run:
    :return: 0 on success, -1 otherwise.
    """

    clp_db_connection_params = database_config.get_clp_connection_params_and_type(True)
    table_prefix = clp_db_connection_params["table_prefix"]

    query = f"""
        DELETE FROM `{table_prefix}archives`
        WHERE begin_timestamp >= %s AND end_timestamp <= %s
        RETURNING id
        """

    return _delete_archives(
        archives_dir, database_config, query, [begin_ts, end_ts], "filter", dry_run
    )


def _delete_archives_by_ids(
    archives_dir: Path,
    database_config: Database,
    archive_ids: List[str],
    dry_run: bool = False,
) -> int:
    """
    Deletes all archives with the specified IDs from both the metadata database and disk.
    :param archives_dir:
    :param database_config:
    :param archive_ids:
    :param dry_run:
    :return: 0 on success, -1 otherwise.
    """
    clp_db_connection_params = database_config.get_clp_connection_params_and_type(True)
    table_prefix = clp_db_connection_params["table_prefix"]

    query = f"""
        DELETE FROM `{table_prefix}archives`
        WHERE id in ({', '.join(['%s'] * len(archive_ids))})
        RETURNING id
        """

    return _delete_archives(archives_dir, database_config, query, archive_ids, "ids", dry_run)


if "__main__" == __name__:
    sys.exit(main(sys.argv))
