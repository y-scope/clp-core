import argparse
import logging
import subprocess
import sys
from pathlib import Path

from clp_py_utils.clp_config import StorageType

from clp_package_utils.general import (
    CLP_DEFAULT_CONFIG_FILE_RELATIVE_PATH,
    dump_container_config,
    generate_container_config,
    generate_container_name,
    generate_container_start_cmd,
    get_clp_home,
    load_config_file,
    validate_and_load_db_credentials_file,
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


def _validate_timestamps(begin_ts, end_ts):
    if begin_ts > end_ts:
        logger.error("begin-ts must be <= end-ts")
        return False
    if end_ts < 0 or begin_ts < 0:
        logger.error("begin-ts and end-ts must be non-negative.")
        return False
    return True


def main(argv):
    clp_home = get_clp_home()
    default_config_file_path = clp_home / CLP_DEFAULT_CONFIG_FILE_RELATIVE_PATH

    # Top-level parser and options
    args_parser = argparse.ArgumentParser(description="View or delete archives.")
    args_parser.add_argument(
        "--config",
        "-c",
        default=str(default_config_file_path),
        help="CLP package configuration file.",
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
        default=0,
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
        help="Delete archives within time frame.",
    )

    # Delete by ID arguments
    del_id_parser.add_argument(
        "ids",
        nargs="+",
        help="List of archive IDs to delete",
    )

    # Delete by filter arguments
    del_filter_parser.add_argument(
        BEGIN_TS_ARG,
        type=int,
        default=0,
        help="Time-range lower-bound (inclusive) as milliseconds from the UNIX epoch.",
    )
    del_filter_parser.add_argument(
        END_TS_ARG,
        type=int,
        required=True,
        help="Time-range upper-bound (include) as milliseconds from the UNIX epoch.",
    )

    parsed_args = args_parser.parse_args(argv[1:])

    # Validate and load config file
    try:
        config_file_path = Path(parsed_args.config)
        clp_config = load_config_file(config_file_path, default_config_file_path, clp_home)
        clp_config.validate_logs_dir()

        # Validate and load necessary credentials
        validate_and_load_db_credentials_file(clp_config, clp_home, False)
    except:
        logger.exception("Failed to load config.")
        return -1

    storage_type = clp_config.archive_output.storage.type
    if StorageType.FS != storage_type:
        logger.error(f"Archive deletion is not supported for storage type: {storage_type}.")
        return -1

    # Validate input depending on subcommands
    if DEL_COMMAND == parsed_args.subcommand:
        if BY_FILTER_COMMAND == parsed_args.del_subcommand:

            # Validate the input timestamp
            if not _validate_timestamps(parsed_args.begin_ts, parsed_args.end_ts):
                return -1

    elif FIND_COMMAND == parsed_args.subcommand:
        if parsed_args.end_ts is not None:

            # Validate the input timestamp
            if not _validate_timestamps(parsed_args.begin_ts, parsed_args.end_ts):
                return -1

    container_name = generate_container_name("archive-manager")

    container_clp_config, mounts = generate_container_config(clp_config, clp_home)
    generated_config_path_on_container, generated_config_path_on_host = dump_container_config(
        container_clp_config, clp_config, container_name
    )

    necessary_mounts = [mounts.clp_home, mounts.logs_dir, mounts.archives_output_dir]
    container_start_cmd = generate_container_start_cmd(
        container_name, necessary_mounts, clp_config.execution_container
    )

    # fmt: off
    archive_manager_cmd = [
        "python3",
        "-m", "clp_package_utils.scripts.native.archive_manager",
        "--config", str(generated_config_path_on_container),
        str(parsed_args.subcommand),
    ]
    # fmt : on
    # Add subcommand-specific arguments
    if DEL_COMMAND == parsed_args.subcommand:
        if parsed_args.dry_run:
            archive_manager_cmd.append(DRY_RUN_ARG)
        if BY_IDS_COMMAND == parsed_args.del_subcommand:
            archive_manager_cmd.append(BY_IDS_COMMAND)
            archive_manager_cmd.extend(parsed_args.ids)
        elif BY_FILTER_COMMAND == parsed_args.del_subcommand:
            archive_manager_cmd.extend([BY_FILTER_COMMAND, str(parsed_args.begin_ts), str(parsed_args.end_ts)])
    elif FIND_COMMAND == parsed_args.subcommand:
        archive_manager_cmd.extend([BEGIN_TS_ARG, str(parsed_args.begin_ts)])
        if parsed_args.end_ts is not None:
            archive_manager_cmd.extend([END_TS_ARG, str(parsed_args.end_ts)])

    cmd = container_start_cmd + archive_manager_cmd

    subprocess.run(cmd, check=True)

    # Remove generated files
    generated_config_path_on_host.unlink()

    return 0


if "__main__" == __name__:
    sys.exit(main(sys.argv))
