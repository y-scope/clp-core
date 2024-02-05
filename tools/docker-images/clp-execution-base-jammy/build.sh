#!/bin/bash

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
repo_root=${script_dir}/../../../

arch=$(uname -m)

if [ "$arch" == "x86_64" ]; then
    arch_name="x86"
elif [ "$arch" == "aarch64" ]; then
    arch_name="arm64"
else
    echo "Error: Unsupported architecture - $arch"
    exit 1
fi

docker build -t clp-execution-x86-ubuntu-focal:dev ${repo_root} --file ${script_dir}/Dockerfile
