import argparse
import json
import logging
import multiprocessing
import os
import pathlib
import socket
import subprocess
import sys
import time
import uuid
from typing import Any, Dict, List, Optional

import yaml
from clp_py_utils.clp_config import (
    ALL_TARGET_NAME,
    CLP_METADATA_TABLE_PREFIX,
    CLPConfig,
    COMPRESSION_JOBS_TABLE_NAME,
    COMPRESSION_SCHEDULER_COMPONENT_NAME,
    COMPRESSION_WORKER_COMPONENT_NAME,
    CONTROLLER_TARGET_NAME,
    DB_COMPONENT_NAME,
    LOG_VIEWER_WEBUI_COMPONENT_NAME,
    QUERY_JOBS_TABLE_NAME,
    QUERY_SCHEDULER_COMPONENT_NAME,
    QUERY_WORKER_COMPONENT_NAME,
    QUEUE_COMPONENT_NAME,
    REDIS_COMPONENT_NAME,
    REDUCER_COMPONENT_NAME,
    RESULTS_CACHE_COMPONENT_NAME,
    StorageType,
    WEBUI_COMPONENT_NAME,
)
from job_orchestration.scheduler.constants import QueueName
from pydantic import BaseModel

from clp_package_utils.general import (
    check_dependencies,
    CLP_DEFAULT_CONFIG_FILE_RELATIVE_PATH,
    CLPDockerMounts,
    CONTAINER_CLP_HOME,
    DockerMount,
    DockerMountType,
    generate_container_config,
    generate_worker_config,
    get_clp_home,
    is_container_exited,
    is_container_running,
    load_config_file,
    validate_and_load_db_credentials_file,
    validate_and_load_queue_credentials_file,
    validate_and_load_redis_credentials_file,
    validate_db_config,
    validate_log_viewer_webui_config,
    validate_queue_config,
    validate_redis_config,
    validate_reducer_config,
    validate_results_cache_config,
    validate_webui_config,
    validate_worker_config,
)

logger = logging.getLogger(__file__)


def container_exists(container_name):
    if is_container_running(container_name):
        logger.info(f"{container_name} already running.")
        return True
    elif is_container_exited(container_name):
        logger.info(f"{container_name} exited but not removed.")
        return True
    return False


def append_docker_port_settings_for_host_ips(
    hostname: str, host_port: int, container_port: int, cmd: [str]
):
    # Note: We use a set because gethostbyname_ex can return the same IP twice for one hostname
    for ip in set(socket.gethostbyname_ex(hostname)[2]):
        cmd.append("-p")
        cmd.append(f"{ip}:{host_port}:{container_port}")


def chown_recursively(
    path: pathlib.Path,
    user_id: int,
    group_id: int,
):
    """
    Recursively changes the owner of the given path to the given user ID and group ID.
    :param path:
    :param user_id:
    :param group_id:
    """
    chown_cmd = ["chown", "--recursive", f"{user_id}:{group_id}", str(path)]
    subprocess.run(chown_cmd, stdout=subprocess.DEVNULL, check=True)


def wait_for_container_cmd(container_name: str, cmd_to_run: [str], timeout: int):
    container_exec_cmd = ["docker", "exec", container_name]
    cmd = container_exec_cmd + cmd_to_run

    begin_time = time.time()

    while True:
        try:
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            return True
        except subprocess.CalledProcessError:
            if time.time() - begin_time > timeout:
                break
            time.sleep(1)

    cmd_str = " ".join(cmd_to_run)
    logger.error(f"Timeout while waiting for command {cmd_str} to run after {timeout} seconds")
    return False


def start_db(instance_id: str, clp_config: CLPConfig, conf_dir: pathlib.Path):
    component_name = DB_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    db_data_dir = clp_config.data_directory / component_name
    db_logs_dir = clp_config.logs_directory / component_name

    validate_db_config(clp_config, db_data_dir, db_logs_dir)

    # Create directories
    db_data_dir.mkdir(exist_ok=True, parents=True)
    db_logs_dir.mkdir(exist_ok=True, parents=True)

    # Start container
    mounts = [
        DockerMount(
            DockerMountType.BIND,
            conf_dir / "mysql" / "conf.d",
            pathlib.Path("/") / "etc" / "mysql" / "conf.d",
            True,
        ),
        DockerMount(DockerMountType.BIND, db_data_dir, pathlib.Path("/") / "var" / "lib" / "mysql"),
        DockerMount(DockerMountType.BIND, db_logs_dir, pathlib.Path("/") / "var" / "log" / "mysql"),
    ]
    # fmt: off
    cmd = [
        "docker", "run",
        "-d",
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"MYSQL_ROOT_PASSWORD={clp_config.database.password}",
        "-e", f"MYSQL_USER={clp_config.database.username}",
        "-e", f"MYSQL_PASSWORD={clp_config.database.password}",
        "-e", f"MYSQL_DATABASE={clp_config.database.name}",
        "-u", f"{os.getuid()}:{os.getgid()}",
    ]
    # fmt: on
    for mount in mounts:
        cmd.append("--mount")
        cmd.append(str(mount))
    append_docker_port_settings_for_host_ips(
        clp_config.database.host, clp_config.database.port, 3306, cmd
    )
    if "mysql" == clp_config.database.type:
        cmd.append("mysql:8.0.23")
    elif "mariadb" == clp_config.database.type:
        cmd.append("mariadb:10.6.4-focal")
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    # fmt: off
    mysqladmin_cmd = [
        "mysqladmin", "ping",
        "--silent",
        "-h", "127.0.0.1",
        "-u", str(clp_config.database.username),
        f"--password={clp_config.database.password}",
    ]
    # fmt: on

    if not wait_for_container_cmd(container_name, mysqladmin_cmd, 180):
        raise EnvironmentError(f"{component_name} did not initialize in time")

    logger.info(f"Started {component_name}.")


def create_db_tables(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    mounts: CLPDockerMounts,
):
    component_name = DB_COMPONENT_NAME
    logger.info(f"Creating {component_name} tables...")

    container_name = f"clp-{component_name}-table-creator-{instance_id}"

    # Create database config file
    db_config_filename = f"{container_name}.yml"
    db_config_file_path = clp_config.logs_directory / db_config_filename
    with open(db_config_file_path, "w") as f:
        yaml.safe_dump(container_clp_config.database.dict(), f)

    clp_site_packages_dir = CONTAINER_CLP_HOME / "lib" / "python3" / "site-packages"
    # fmt: off
    container_start_cmd = [
        "docker", "run",
        "-i",
        "--network", "host",
        "--rm",
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"PYTHONPATH={clp_site_packages_dir}",
        "-u", f"{os.getuid()}:{os.getgid()}",
        "--mount", str(mounts.clp_home),
    ]
    # fmt: on
    necessary_mounts = [mounts.data_dir, mounts.logs_dir]
    for mount in necessary_mounts:
        if mount:
            container_start_cmd.append("--mount")
            container_start_cmd.append(str(mount))
    container_start_cmd.append(clp_config.execution_container)

    clp_py_utils_dir = clp_site_packages_dir / "clp_py_utils"
    # fmt: off
    create_tables_cmd = [
        "python3",
        str(clp_py_utils_dir / "create-db-tables.py"),
        "--config", str(container_clp_config.logs_directory / db_config_filename),
    ]
    # fmt: on

    cmd = container_start_cmd + create_tables_cmd
    logger.debug(" ".join(cmd))
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    db_config_file_path.unlink()

    logger.info(f"Created {component_name} tables.")


def create_results_cache_indices(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    mounts: CLPDockerMounts,
):
    component_name = RESULTS_CACHE_COMPONENT_NAME
    logger.info(f"Creating {component_name} indices...")

    container_name = f"clp-{component_name}-indices-creator-{instance_id}"

    clp_site_packages_dir = CONTAINER_CLP_HOME / "lib" / "python3" / "site-packages"
    # fmt: off
    container_start_cmd = [
        "docker", "run",
        "-i",
        "--network", "host",
        "--rm",
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"PYTHONPATH={clp_site_packages_dir}",
        "-u", f"{os.getuid()}:{os.getgid()}",
    ]
    # fmt: on
    necessary_mounts = [mounts.clp_home, mounts.data_dir, mounts.logs_dir]
    for mount in necessary_mounts:
        if mount:
            container_start_cmd.append("--mount")
            container_start_cmd.append(str(mount))
    container_start_cmd.append(clp_config.execution_container)

    clp_py_utils_dir = clp_site_packages_dir / "clp_py_utils"
    # fmt: off
    create_tables_cmd = [
        "python3",
        str(clp_py_utils_dir / "create-results-cache-indices.py"),
        "--uri", container_clp_config.results_cache.get_uri(),
        "--stream-collection", container_clp_config.results_cache.stream_collection_name,
    ]
    # fmt: on

    cmd = container_start_cmd + create_tables_cmd
    logger.debug(" ".join(cmd))
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Created {component_name} indices.")


def start_queue(instance_id: str, clp_config: CLPConfig):
    component_name = QUEUE_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    queue_logs_dir = clp_config.logs_directory / component_name
    validate_queue_config(clp_config, queue_logs_dir)

    log_filename = "rabbitmq.log"

    # Generate config file
    config_filename = f"{container_name}.conf"
    host_config_file_path = clp_config.logs_directory / config_filename
    with open(host_config_file_path, "w") as f:
        f.write(f"default_user = {clp_config.queue.username}\n")
        f.write(f"default_pass = {clp_config.queue.password}\n")
        f.write(f"log.file = {log_filename}\n")

    # Create directories
    queue_logs_dir.mkdir(exist_ok=True, parents=True)

    # Start container
    rabbitmq_logs_dir = pathlib.Path("/") / "var" / "log" / "rabbitmq"
    mounts = [
        DockerMount(
            DockerMountType.BIND,
            host_config_file_path,
            pathlib.Path("/") / "etc" / "rabbitmq" / "rabbitmq.conf",
            True,
        ),
        DockerMount(DockerMountType.BIND, queue_logs_dir, rabbitmq_logs_dir),
    ]
    rabbitmq_pid_file_path = pathlib.Path("/") / "tmp" / "rabbitmq.pid"

    host_user_id = os.getuid()
    if 0 != host_user_id:
        container_user = f"{host_user_id}:{os.getgid()}"
    else:
        # The host user is `root` so use the container's default user and make this component's
        # directories writable by that user.
        # NOTE: This doesn't affect the host user's access to the directories since they're `root`.
        container_user = "rabbitmq"
        default_container_user_id = 999
        default_container_group_id = 999
        chown_recursively(queue_logs_dir, default_container_user_id, default_container_group_id)

    # fmt: off
    cmd = [
        "docker", "run",
        "-d",
        "--name", container_name,
        "--log-driver", "local",
        # Override RABBITMQ_LOGS since the image sets it to *only* log to stdout
        "-e", f"RABBITMQ_LOGS={rabbitmq_logs_dir / log_filename}",
        "-e", f"RABBITMQ_PID_FILE={rabbitmq_pid_file_path}",
        "-u", container_user
    ]
    # fmt: on
    append_docker_port_settings_for_host_ips(
        clp_config.queue.host, clp_config.queue.port, 5672, cmd
    )
    for mount in mounts:
        cmd.append("--mount")
        cmd.append(str(mount))
    cmd.append("rabbitmq:3.9.8")
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    # Wait for queue to start up
    rabbitmq_cmd = ["rabbitmq-diagnostics", "check_running"]
    if not wait_for_container_cmd(container_name, rabbitmq_cmd, 60):
        raise EnvironmentError(f"{component_name} did not initialize in time")

    logger.info(f"Started {component_name}.")


def start_redis(instance_id: str, clp_config: CLPConfig, conf_dir: pathlib.Path):
    component_name = REDIS_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    redis_logs_dir = clp_config.logs_directory / component_name
    redis_data_dir = clp_config.data_directory / component_name

    base_config_file_path = conf_dir / "redis" / "redis.conf"
    validate_redis_config(clp_config, redis_data_dir, redis_logs_dir, base_config_file_path)

    config_filename = f"{container_name}.conf"
    host_config_file_path = clp_config.logs_directory / config_filename
    with open(base_config_file_path, "r") as base, open(host_config_file_path, "w") as full:
        for line in base.readlines():
            full.write(line)
        full.write(f"requirepass {clp_config.redis.password}\n")

    redis_data_dir.mkdir(exist_ok=True, parents=True)
    redis_logs_dir.mkdir(exist_ok=True, parents=True)

    # Start container
    config_file_path = pathlib.Path("/") / "usr" / "local" / "etc" / "redis" / "redis.conf"
    mounts = [
        DockerMount(DockerMountType.BIND, host_config_file_path, config_file_path, True),
        DockerMount(
            DockerMountType.BIND, redis_logs_dir, pathlib.Path("/") / "var" / "log" / "redis"
        ),
        DockerMount(DockerMountType.BIND, redis_data_dir, pathlib.Path("/") / "data"),
    ]

    host_user_id = os.getuid()
    if 0 != host_user_id:
        container_user = f"{host_user_id}:{os.getgid()}"
    else:
        # The host user is `root` so use the container's default user and make this component's
        # directories writable by that user.
        # NOTE: This doesn't affect the host user's access to the directories since they're `root`.
        container_user = "redis"
        default_container_user_id = 999
        default_container_group_id = 999
        chown_recursively(redis_data_dir, default_container_user_id, default_container_group_id)
        chown_recursively(redis_logs_dir, default_container_user_id, default_container_group_id)

    # fmt: off
    cmd = [
        "docker", "run",
        "-d",
        "--name", container_name,
        "--log-driver", "local",
        "-u", container_user,
    ]
    # fmt: on
    for mount in mounts:
        cmd.append("--mount")
        cmd.append(str(mount))
    append_docker_port_settings_for_host_ips(
        clp_config.redis.host, clp_config.redis.port, 6379, cmd
    )
    cmd.append("redis:7.2.4")
    cmd.append("redis-server")
    cmd.append(str(config_file_path))
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    # fmt: off
    redis_ping_cmd = [
        "redis-cli",
        "-h", "127.0.0.1",
        "-p", "6379",
        "-a", clp_config.redis.password,
        "PING"
    ]
    # fmt: on

    if not wait_for_container_cmd(container_name, redis_ping_cmd, 30):
        raise EnvironmentError(f"{component_name} did not initialize in time")

    logger.info(f"Started {component_name}.")


def start_results_cache(instance_id: str, clp_config: CLPConfig, conf_dir: pathlib.Path):
    component_name = RESULTS_CACHE_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    data_dir = clp_config.data_directory / component_name
    logs_dir = clp_config.logs_directory / component_name

    validate_results_cache_config(clp_config, data_dir, logs_dir)

    data_dir.mkdir(exist_ok=True, parents=True)
    logs_dir.mkdir(exist_ok=True, parents=True)

    mounts = [
        DockerMount(
            DockerMountType.BIND, conf_dir / "mongo", pathlib.Path("/") / "etc" / "mongo", True
        ),
        DockerMount(DockerMountType.BIND, data_dir, pathlib.Path("/") / "data" / "db"),
        DockerMount(DockerMountType.BIND, logs_dir, pathlib.Path("/") / "var" / "log" / "mongodb"),
    ]

    host_user_id = os.getuid()
    if 0 != host_user_id:
        container_user = f"{host_user_id}:{os.getgid()}"
    else:
        # The host user is `root` so use the container's default user and make this component's
        # directories writable by that user.
        # NOTE: This doesn't affect the host user's access to the directories since they're `root`.
        container_user = "mongodb"
        default_container_user_id = 999
        default_container_group_id = 999
        chown_recursively(data_dir, default_container_user_id, default_container_group_id)
        chown_recursively(logs_dir, default_container_user_id, default_container_group_id)

    # fmt: off
    cmd = [
        "docker", "run",
        "-d",
        "--name", container_name,
        "--log-driver", "local",
        "-u", container_user,
    ]
    # fmt: on
    for mount in mounts:
        cmd.append("--mount")
        cmd.append(str(mount))
    append_docker_port_settings_for_host_ips(
        clp_config.results_cache.host, clp_config.results_cache.port, 27017, cmd
    )
    cmd.append("mongo:7.0.1")
    cmd.append("--config")
    cmd.append(str(pathlib.Path("/") / "etc" / "mongo" / "mongod.conf"))
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Started {component_name}.")


def start_compression_scheduler(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    mounts: CLPDockerMounts,
):
    module_name = "job_orchestration.scheduler.compress.compression_scheduler"
    generic_start_scheduler(
        COMPRESSION_SCHEDULER_COMPONENT_NAME,
        module_name,
        instance_id,
        clp_config,
        container_clp_config,
        mounts,
    )


def start_query_scheduler(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    mounts: CLPDockerMounts,
):
    module_name = "job_orchestration.scheduler.query.query_scheduler"
    generic_start_scheduler(
        QUERY_SCHEDULER_COMPONENT_NAME,
        module_name,
        instance_id,
        clp_config,
        container_clp_config,
        mounts,
    )


def generic_start_scheduler(
    component_name: str,
    module_name: str,
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    mounts: CLPDockerMounts,
):
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    container_config_filename = f"{container_name}.yml"
    container_config_file_path = clp_config.logs_directory / container_config_filename
    with open(container_config_file_path, "w") as f:
        yaml.safe_dump(container_clp_config.dump_to_primitive_dict(), f)

    logs_dir = clp_config.logs_directory / component_name
    logs_dir.mkdir(parents=True, exist_ok=True)
    container_logs_dir = container_clp_config.logs_directory / component_name

    clp_site_packages_dir = CONTAINER_CLP_HOME / "lib" / "python3" / "site-packages"
    # fmt: off
    container_start_cmd = [
        "docker", "run",
        "-di",
        "--network", "host",
        "-w", str(CONTAINER_CLP_HOME),
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"PYTHONPATH={clp_site_packages_dir}",
        "-e", (
            f"BROKER_URL=amqp://"
            f"{container_clp_config.queue.username}:{container_clp_config.queue.password}@"
            f"{container_clp_config.queue.host}:{container_clp_config.queue.port}"
        ),
        "-e", (
            f"RESULT_BACKEND=redis://default:{container_clp_config.redis.password}@"
            f"{container_clp_config.redis.host}:{container_clp_config.redis.port}/"
            f"{container_clp_config.redis.query_backend_database}"
        ),
        "-e", f"CLP_LOGS_DIR={container_logs_dir}",
        "-e", f"CLP_LOGGING_LEVEL={clp_config.query_scheduler.logging_level}",
        "-u", f"{os.getuid()}:{os.getgid()}",
        "--mount", str(mounts.clp_home),
    ]
    # fmt: on
    necessary_mounts = [
        mounts.logs_dir,
    ]
    if COMPRESSION_SCHEDULER_COMPONENT_NAME == component_name:
        necessary_mounts.append(mounts.input_logs_dir)
    for mount in necessary_mounts:
        if mount:
            container_start_cmd.append("--mount")
            container_start_cmd.append(str(mount))
    container_start_cmd.append(clp_config.execution_container)

    # fmt: off
    scheduler_cmd = [
        "python3", "-u",
        "-m", module_name,
        "--config", str(container_clp_config.logs_directory / container_config_filename),
    ]
    # fmt: on
    cmd = container_start_cmd + scheduler_cmd
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Started {component_name}.")


def start_compression_worker(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    num_cpus: int,
    mounts: CLPDockerMounts,
):
    celery_method = "job_orchestration.executor.compress"
    celery_route = f"{QueueName.COMPRESSION}"
    compression_worker_mounts = [mounts.archives_output_dir]
    generic_start_worker(
        COMPRESSION_WORKER_COMPONENT_NAME,
        instance_id,
        clp_config,
        clp_config.compression_worker,
        container_clp_config,
        celery_method,
        celery_route,
        clp_config.redis.compression_backend_database,
        num_cpus,
        mounts,
        compression_worker_mounts,
    )


def start_query_worker(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    num_cpus: int,
    mounts: CLPDockerMounts,
):
    celery_method = "job_orchestration.executor.query"
    celery_route = f"{QueueName.QUERY}"

    query_worker_mounts = [mounts.stream_output_dir]
    if clp_config.archive_output.storage.type == StorageType.FS:
        query_worker_mounts.append(mounts.archives_output_dir)

    generic_start_worker(
        QUERY_WORKER_COMPONENT_NAME,
        instance_id,
        clp_config,
        clp_config.query_worker,
        container_clp_config,
        celery_method,
        celery_route,
        clp_config.redis.query_backend_database,
        num_cpus,
        mounts,
        query_worker_mounts,
    )


def generic_start_worker(
    component_name: str,
    instance_id: str,
    clp_config: CLPConfig,
    worker_config: BaseModel,
    container_clp_config: CLPConfig,
    celery_method: str,
    celery_route: str,
    redis_database: int,
    num_cpus: int,
    mounts: CLPDockerMounts,
    worker_specific_mount: Optional[List[Optional[DockerMount]]],
):
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    container_config_filename = f"{container_name}.yml"
    container_config_file_path = clp_config.logs_directory / container_config_filename
    container_worker_config = generate_worker_config(container_clp_config)
    with open(container_config_file_path, "w") as f:
        yaml.safe_dump(container_worker_config.dump_to_primitive_dict(), f)

    logs_dir = clp_config.logs_directory / component_name
    logs_dir.mkdir(parents=True, exist_ok=True)
    container_logs_dir = container_clp_config.logs_directory / component_name

    # Create necessary directories
    clp_config.archive_output.get_directory().mkdir(parents=True, exist_ok=True)
    clp_config.stream_output.directory.mkdir(parents=True, exist_ok=True)

    clp_site_packages_dir = CONTAINER_CLP_HOME / "lib" / "python3" / "site-packages"
    # fmt: off
    container_start_cmd = [
        "docker", "run",
        "-di",
        "--network", "host",
        "-w", str(CONTAINER_CLP_HOME),
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"PYTHONPATH={clp_site_packages_dir}",
        "-e", (
            f"BROKER_URL=amqp://"
            f"{container_clp_config.queue.username}:{container_clp_config.queue.password}@"
            f"{container_clp_config.queue.host}:{container_clp_config.queue.port}"
        ),
        "-e", (
            f"RESULT_BACKEND=redis://default:{container_clp_config.redis.password}@"
            f"{container_clp_config.redis.host}:{container_clp_config.redis.port}/{redis_database}"
        ),
        "-e", f"CLP_HOME={CONTAINER_CLP_HOME}",
        "-e", f"CLP_CONFIG_PATH={container_clp_config.logs_directory / container_config_filename}",
        "-e", f"CLP_LOGS_DIR={container_logs_dir}",
        "-e", f"CLP_LOGGING_LEVEL={worker_config.logging_level}",
        "-u", f"{os.getuid()}:{os.getgid()}",
    ]
    # fmt: on

    necessary_mounts = [
        mounts.clp_home,
        mounts.data_dir,
        mounts.logs_dir,
        mounts.input_logs_dir,
    ]
    if worker_specific_mount:
        necessary_mounts.extend(worker_specific_mount)

    for mount in necessary_mounts:
        if not mount:
            raise ValueError(f"Required mount configuration is empty: {necessary_mounts}")
        container_start_cmd.append("--mount")
        container_start_cmd.append(str(mount))
    container_start_cmd.append(clp_config.execution_container)

    worker_cmd = [
        "python3",
        str(clp_site_packages_dir / "bin" / "celery"),
        "-A",
        celery_method,
        "worker",
        "--concurrency",
        str(num_cpus),
        "--loglevel",
        "WARNING",
        "-f",
        str(container_logs_dir / "worker.log"),
        "-Q",
        celery_route,
        "-n",
        component_name,
    ]
    cmd = container_start_cmd + worker_cmd
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Started {component_name}.")


def update_settings_object(
    parent_key_prefix: str,
    settings: Dict[str, Any],
    updates: Dict[str, Any],
):
    """
    Recursively updates the given settings object with the values from `updates`.

    :param parent_key_prefix: The prefix for keys at this level in the settings dictionary.
    :param settings: The settings to update.
    :param updates: The updates.
    :raises ValueError: If a key in `updates` doesn't exist in `settings`.
    """
    for key, value in updates.items():
        if key not in settings:
            error_msg = f"{parent_key_prefix}{key} is not a valid configuration key for the webui."
            raise ValueError(error_msg)
        if isinstance(value, dict):
            update_settings_object(f"{parent_key_prefix}{key}.", settings[key], value)
        else:
            settings[key] = updates[key]


def read_and_update_settings_json(settings_file_path: pathlib.Path, updates: Dict[str, Any]):
    """
    Reads and updates a settings JSON file.

    :param settings_file_path:
    :param updates:
    """
    with open(settings_file_path, "r") as settings_json_file:
        settings_object = json.loads(settings_json_file.read())
    update_settings_object("", settings_object, updates)

    return settings_object


def start_webui(instance_id: str, clp_config: CLPConfig, mounts: CLPDockerMounts):
    component_name = WEBUI_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    webui_logs_dir = clp_config.logs_directory / component_name
    container_webui_dir = CONTAINER_CLP_HOME / "var" / "www" / "webui"
    node_path = str(container_webui_dir / "programs" / "server" / "npm" / "node_modules")
    settings_json_path = get_clp_home() / "var" / "www" / "webui" / "settings.json"

    validate_webui_config(clp_config, webui_logs_dir, settings_json_path)

    # Create directories
    webui_logs_dir.mkdir(exist_ok=True, parents=True)

    container_webui_logs_dir = pathlib.Path("/") / "var" / "log" / component_name

    # Read and update settings.json
    meteor_settings_updates = {
        "private": {
            "SqlDbHost": clp_config.database.host,
            "SqlDbPort": clp_config.database.port,
            "SqlDbName": clp_config.database.name,
            "SqlDbClpArchivesTableName": f"{CLP_METADATA_TABLE_PREFIX}archives",
            "SqlDbClpFilesTableName": f"{CLP_METADATA_TABLE_PREFIX}files",
            "SqlDbCompressionJobsTableName": COMPRESSION_JOBS_TABLE_NAME,
            "SqlDbQueryJobsTableName": QUERY_JOBS_TABLE_NAME,
        },
        "public": {
            "ClpStorageEngine": clp_config.package.storage_engine,
            "LogViewerWebuiUrl": (
                f"http://{clp_config.log_viewer_webui.host}:{clp_config.log_viewer_webui.port}",
            ),
        },
    }
    meteor_settings = read_and_update_settings_json(settings_json_path, meteor_settings_updates)

    # Start container
    # fmt: off
    container_cmd = [
        "docker", "run",
        "-d",
        "--network", "host",
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"NODE_PATH={node_path}",
        "-e", f"MONGO_URL={clp_config.results_cache.get_uri()}",
        "-e", f"PORT={clp_config.webui.port}",
        "-e", f"ROOT_URL=http://{clp_config.webui.host}",
        "-e", f"METEOR_SETTINGS={json.dumps(meteor_settings)}",
        "-e", f"CLP_DB_USER={clp_config.database.username}",
        "-e", f"CLP_DB_PASS={clp_config.database.password}",
        "-e", f"WEBUI_LOGS_DIR={container_webui_logs_dir}",
        "-e", f"WEBUI_LOGGING_LEVEL={clp_config.webui.logging_level}",
        "-u", f"{os.getuid()}:{os.getgid()}",
    ]
    # fmt: on
    necessary_mounts = [
        mounts.clp_home,
        DockerMount(DockerMountType.BIND, webui_logs_dir, container_webui_logs_dir),
    ]
    for mount in necessary_mounts:
        if mount:
            container_cmd.append("--mount")
            container_cmd.append(str(mount))
    container_cmd.append(clp_config.execution_container)

    node_cmd = [
        str(CONTAINER_CLP_HOME / "bin" / "node-14"),
        str(container_webui_dir / "launcher.js"),
        str(container_webui_dir / "main.js"),
    ]
    cmd = container_cmd + node_cmd
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Started {component_name}.")


def start_log_viewer_webui(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    mounts: CLPDockerMounts,
):
    component_name = LOG_VIEWER_WEBUI_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    container_log_viewer_webui_dir = CONTAINER_CLP_HOME / "var" / "www" / "log-viewer-webui"
    node_path = str(container_log_viewer_webui_dir / "server" / "node_modules")
    settings_json_path = (
        get_clp_home() / "var" / "www" / "log-viewer-webui" / "server" / "dist" / "settings.json"
    )

    validate_log_viewer_webui_config(clp_config, settings_json_path)

    # Read, update, and write back settings.json
    settings_json_updates = {
        "SqlDbHost": clp_config.database.host,
        "SqlDbPort": clp_config.database.port,
        "SqlDbName": clp_config.database.name,
        "SqlDbQueryJobsTableName": QUERY_JOBS_TABLE_NAME,
        "MongoDbHost": clp_config.results_cache.host,
        "MongoDbPort": clp_config.results_cache.port,
        "MongoDbName": clp_config.results_cache.db_name,
        "MongoDbStreamFilesCollectionName": clp_config.results_cache.stream_collection_name,
        "ClientDir": str(container_log_viewer_webui_dir / "client"),
        "StreamFilesDir": str(container_clp_config.stream_output.directory),
        "StreamTargetUncompressedSize": container_clp_config.stream_output.target_uncompressed_size,
        "LogViewerDir": str(container_log_viewer_webui_dir / "yscope-log-viewer"),
    }
    settings_json = read_and_update_settings_json(settings_json_path, settings_json_updates)
    with open(settings_json_path, "w") as settings_json_file:
        settings_json_file.write(json.dumps(settings_json))

    # Start container
    # fmt: off
    container_cmd = [
        "docker", "run",
        "-d",
        "--network", "host",
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"NODE_PATH={node_path}",
        "-e", f"HOST={clp_config.log_viewer_webui.host}",
        "-e", f"PORT={clp_config.log_viewer_webui.port}",
        "-e", f"CLP_DB_USER={clp_config.database.username}",
        "-e", f"CLP_DB_PASS={clp_config.database.password}",
        "-e", f"NODE_ENV=production",
        "-u", f"{os.getuid()}:{os.getgid()}",
    ]
    # fmt: on
    necessary_mounts = [
        mounts.clp_home,
        mounts.stream_output_dir,
    ]
    for mount in necessary_mounts:
        if mount:
            container_cmd.append("--mount")
            container_cmd.append(str(mount))
    container_cmd.append(clp_config.execution_container)

    node_cmd = [
        str(CONTAINER_CLP_HOME / "bin" / "node-22"),
        str(container_log_viewer_webui_dir / "server" / "dist" / "src" / "main.js"),
    ]
    cmd = container_cmd + node_cmd
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Started {component_name}.")


def start_reducer(
    instance_id: str,
    clp_config: CLPConfig,
    container_clp_config: CLPConfig,
    num_workers: int,
    mounts: CLPDockerMounts,
):
    component_name = REDUCER_COMPONENT_NAME
    logger.info(f"Starting {component_name}...")

    container_name = f"clp-{component_name}-{instance_id}"
    if container_exists(container_name):
        return

    container_config_filename = f"{container_name}.yml"
    container_config_file_path = clp_config.logs_directory / container_config_filename
    with open(container_config_file_path, "w") as f:
        yaml.safe_dump(container_clp_config.dump_to_primitive_dict(), f)

    logs_dir = clp_config.logs_directory / component_name
    validate_reducer_config(clp_config, logs_dir, num_workers)

    logs_dir.mkdir(parents=True, exist_ok=True)
    container_logs_dir = container_clp_config.logs_directory / component_name

    clp_site_packages_dir = CONTAINER_CLP_HOME / "lib" / "python3" / "site-packages"
    # fmt: off
    container_start_cmd = [
        "docker", "run",
        "-di",
        "--network", "host",
        "-w", str(CONTAINER_CLP_HOME),
        "--name", container_name,
        "--log-driver", "local",
        "-e", f"PYTHONPATH={clp_site_packages_dir}",
        "-e", f"CLP_LOGS_DIR={container_logs_dir}",
        "-e", f"CLP_LOGGING_LEVEL={clp_config.reducer.logging_level}",
        "-e", f"CLP_HOME={CONTAINER_CLP_HOME}",
        "-u", f"{os.getuid()}:{os.getgid()}",
        "--mount", str(mounts.clp_home),
    ]
    # fmt: on
    necessary_mounts = [
        mounts.logs_dir,
    ]
    for mount in necessary_mounts:
        if mount:
            container_start_cmd.append("--mount")
            container_start_cmd.append(str(mount))
    container_start_cmd.append(clp_config.execution_container)

    # fmt: off
    reducer_cmd = [
        "python3", "-u",
        "-m", "job_orchestration.reducer.reducer",
        "--config", str(container_clp_config.logs_directory / container_config_filename),
        "--concurrency", str(num_workers),
        "--upsert-interval", str(clp_config.reducer.upsert_interval),
    ]
    # fmt: on
    cmd = container_start_cmd + reducer_cmd
    subprocess.run(cmd, stdout=subprocess.DEVNULL, check=True)

    logger.info(f"Started {component_name}.")


def add_num_workers_argument(parser):
    parser.add_argument(
        "--num-workers",
        type=int,
        default=multiprocessing.cpu_count(),
        help="Number of workers to start",
    )


def main(argv):
    clp_home = get_clp_home()
    default_config_file_path = clp_home / CLP_DEFAULT_CONFIG_FILE_RELATIVE_PATH

    args_parser = argparse.ArgumentParser(description="Starts CLP")
    args_parser.add_argument(
        "--config",
        "-c",
        default=str(default_config_file_path),
        help="CLP package configuration file.",
    )

    component_args_parser = args_parser.add_subparsers(dest="target")
    component_args_parser.add_parser(CONTROLLER_TARGET_NAME)
    component_args_parser.add_parser(DB_COMPONENT_NAME)
    component_args_parser.add_parser(QUEUE_COMPONENT_NAME)
    component_args_parser.add_parser(REDIS_COMPONENT_NAME)
    component_args_parser.add_parser(RESULTS_CACHE_COMPONENT_NAME)
    component_args_parser.add_parser(COMPRESSION_SCHEDULER_COMPONENT_NAME)
    component_args_parser.add_parser(QUERY_SCHEDULER_COMPONENT_NAME)
    compression_worker_parser = component_args_parser.add_parser(COMPRESSION_WORKER_COMPONENT_NAME)
    add_num_workers_argument(compression_worker_parser)
    query_worker_parser = component_args_parser.add_parser(QUERY_WORKER_COMPONENT_NAME)
    add_num_workers_argument(query_worker_parser)
    reducer_server_parser = component_args_parser.add_parser(REDUCER_COMPONENT_NAME)
    add_num_workers_argument(reducer_server_parser)
    component_args_parser.add_parser(WEBUI_COMPONENT_NAME)
    component_args_parser.add_parser(LOG_VIEWER_WEBUI_COMPONENT_NAME)

    parsed_args = args_parser.parse_args(argv[1:])

    if parsed_args.target:
        target = parsed_args.target
    else:
        target = ALL_TARGET_NAME

    try:
        check_dependencies()
    except:
        logger.exception("Dependency checking failed.")
        return -1

    # Validate and load config file
    try:
        config_file_path = pathlib.Path(parsed_args.config)
        clp_config = load_config_file(config_file_path, default_config_file_path, clp_home)

        # Validate and load necessary credentials
        if target in (
            ALL_TARGET_NAME,
            CONTROLLER_TARGET_NAME,
            DB_COMPONENT_NAME,
            COMPRESSION_SCHEDULER_COMPONENT_NAME,
            QUERY_SCHEDULER_COMPONENT_NAME,
            WEBUI_COMPONENT_NAME,
            LOG_VIEWER_WEBUI_COMPONENT_NAME,
        ):
            validate_and_load_db_credentials_file(clp_config, clp_home, True)
        if target in (
            ALL_TARGET_NAME,
            CONTROLLER_TARGET_NAME,
            QUEUE_COMPONENT_NAME,
            COMPRESSION_SCHEDULER_COMPONENT_NAME,
            QUERY_SCHEDULER_COMPONENT_NAME,
            COMPRESSION_WORKER_COMPONENT_NAME,
            QUERY_WORKER_COMPONENT_NAME,
        ):
            validate_and_load_queue_credentials_file(clp_config, clp_home, True)
        if target in (
            ALL_TARGET_NAME,
            CONTROLLER_TARGET_NAME,
            REDIS_COMPONENT_NAME,
            COMPRESSION_SCHEDULER_COMPONENT_NAME,
            QUERY_SCHEDULER_COMPONENT_NAME,
            COMPRESSION_WORKER_COMPONENT_NAME,
            QUERY_WORKER_COMPONENT_NAME,
        ):
            validate_and_load_redis_credentials_file(clp_config, clp_home, True)
        if target in (
            ALL_TARGET_NAME,
            COMPRESSION_WORKER_COMPONENT_NAME,
            QUERY_WORKER_COMPONENT_NAME,
        ):
            validate_worker_config(clp_config)

        clp_config.validate_data_dir()
        clp_config.validate_logs_dir()
    except:
        logger.exception("Failed to load config.")
        return -1

    if target in (
        COMPRESSION_WORKER_COMPONENT_NAME,
        REDUCER_COMPONENT_NAME,
        QUERY_WORKER_COMPONENT_NAME,
    ):
        num_workers = parsed_args.num_workers
    else:
        num_workers = multiprocessing.cpu_count() // 2

    container_clp_config, mounts = generate_container_config(clp_config, clp_home)

    # Create necessary directories
    clp_config.data_directory.mkdir(parents=True, exist_ok=True)
    clp_config.logs_directory.mkdir(parents=True, exist_ok=True)

    try:
        # Create instance-id file
        instance_id_file_path = clp_config.logs_directory / "instance-id"
        if instance_id_file_path.exists():
            with open(instance_id_file_path, "r") as f:
                instance_id = f.readline()
        else:
            instance_id = str(uuid.uuid4())[-4:]
            with open(instance_id_file_path, "w") as f:
                f.write(instance_id)
                f.flush()

        conf_dir = clp_home / "etc"

        # Start components
        if target in (ALL_TARGET_NAME, DB_COMPONENT_NAME):
            start_db(instance_id, clp_config, conf_dir)
        if target in (ALL_TARGET_NAME, CONTROLLER_TARGET_NAME, DB_COMPONENT_NAME):
            create_db_tables(instance_id, clp_config, container_clp_config, mounts)
        if target in (ALL_TARGET_NAME, CONTROLLER_TARGET_NAME, QUEUE_COMPONENT_NAME):
            start_queue(instance_id, clp_config)
        if target in (ALL_TARGET_NAME, CONTROLLER_TARGET_NAME, REDIS_COMPONENT_NAME):
            start_redis(instance_id, clp_config, conf_dir)
        if target in (ALL_TARGET_NAME, RESULTS_CACHE_COMPONENT_NAME):
            start_results_cache(instance_id, clp_config, conf_dir)
        if target in (ALL_TARGET_NAME, CONTROLLER_TARGET_NAME, RESULTS_CACHE_COMPONENT_NAME):
            create_results_cache_indices(instance_id, clp_config, container_clp_config, mounts)
        if target in (
            ALL_TARGET_NAME,
            CONTROLLER_TARGET_NAME,
            COMPRESSION_SCHEDULER_COMPONENT_NAME,
        ):
            start_compression_scheduler(instance_id, clp_config, container_clp_config, mounts)
        if target in (ALL_TARGET_NAME, CONTROLLER_TARGET_NAME, QUERY_SCHEDULER_COMPONENT_NAME):
            start_query_scheduler(instance_id, clp_config, container_clp_config, mounts)
        if target in (ALL_TARGET_NAME, COMPRESSION_WORKER_COMPONENT_NAME):
            start_compression_worker(
                instance_id, clp_config, container_clp_config, num_workers, mounts
            )
        if target in (ALL_TARGET_NAME, QUERY_WORKER_COMPONENT_NAME):
            start_query_worker(instance_id, clp_config, container_clp_config, num_workers, mounts)
        if target in (ALL_TARGET_NAME, REDUCER_COMPONENT_NAME):
            start_reducer(instance_id, clp_config, container_clp_config, num_workers, mounts)
        if target in (ALL_TARGET_NAME, WEBUI_COMPONENT_NAME):
            start_webui(instance_id, clp_config, mounts)
        if target in (ALL_TARGET_NAME, LOG_VIEWER_WEBUI_COMPONENT_NAME):
            start_log_viewer_webui(instance_id, clp_config, container_clp_config, mounts)

    except Exception as ex:
        if type(ex) == ValueError:
            logger.error(f"Failed to start CLP: {ex}")
        else:
            logger.exception("Failed to start CLP.")
        return -1

    return 0


if "__main__" == __name__:
    sys.exit(main(sys.argv))
