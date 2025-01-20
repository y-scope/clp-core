#!/usr/bin/env bash

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
package_root="$script_dir/../.."

PYTHONPATH=$(readlink -f "$package_root/lib/python3/site-packages") \
    python3 \
    -m clp_package_utils.scripts.archive_manager \
    "$@"
