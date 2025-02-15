import argparse
import os
import pathlib
import shutil
import stat
import subprocess


def get_architecture() -> str:
    """
    Detect the system architecture using `dpkg`.

    Returns:
        str: The system architecture (e.g., 'amd64', 'arm64').

    Raises:
        RuntimeError: If the architecture cannot be detected.
    """
    try:
        result = subprocess.run(
            ["dpkg", "--print-architecture"], capture_output=True, text=True, check=True
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to detect architecture: {e.stderr}") from e


def create_debian_package(args):
    """
    Create a Debian package for CLP core.

    Args:
        args: Command-line arguments containing build directory, version, revision,
              and output directory.
    """

    # Package configuration
    package_name = "clp-core"
    maintainer = "YScope <admin@yscope.com>"
    description = "A minimal CLP core Debian package"
    full_version = f"{args.version}-{args.revision}"
    package_dependencies = ["libmariadb-dev", "libssl-dev"]

    # Get system architecture (e.g., `amd64`, `arm64`)
    architecture = get_architecture()

    # Define package directory structure
    package_dir = pathlib.Path(args.output_dir) / f"{package_name}-{full_version}"
    debian_dir = package_dir / "DEBIAN"
    bin_dir = package_dir / "usr" / "local" / "bin"

    # Clean up existing directory if it exists
    if package_dir.is_dir():
        shutil.rmtree(package_dir, ignore_errors=True)

    # Create debian package directory structure
    debian_dir.mkdir(parents=True, exist_ok=False)
    bin_dir.mkdir(parents=True, exist_ok=False)

    # Create the Debian control file
    control_file_lines = []
    control_file_lines.append(f"Package: {package_name}")
    control_file_lines.append(f"Maintainer: {maintainer}")
    control_file_lines.append(f"Version: {full_version}")
    control_file_lines.append(f"Architecture: {architecture}")
    control_file_lines.append(f"Depends: {', '.join(package_dependencies)}")
    control_file_lines.append(f"Description: {description}")
    control_file_lines.append(os.linesep)
    (debian_dir / "control").write_text(os.linesep.join(control_file_lines))

    # Copy CLP core binaries from the build directory to the package's binary directory
    print("\n=========================================================================\n")
    build_dir = pathlib.Path(args.build_dir)
    if not build_dir.is_dir():
        raise ValueError(f"Build directory does not exist: {build_dir}")

    binaries = ["clg", "clo", "clp", "clp-s", "glt", "indexer", "make-dictionaries-readable"]
    for binary in binaries:
        binary_path = build_dir / binary
        if not binary_path.is_file():
            raise ValueError(f"Required binary not found: {binary_path}")
        print(f"Copying clp-core binary `{binary}` to {bin_dir.relative_to(package_dir)}")
        shutil.copy(build_dir / binary, bin_dir)

    # Make all binaries executable
    for path in bin_dir.glob("*"):  # Non-recursive iteration
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    # Build the package
    print("\n=========================================================================\n")
    deb_file = pathlib.Path(args.output_dir) / f"{package_name}_{full_version}_{architecture}.deb"
    try:
        subprocess.run(
            ["dpkg-deb", "--build", str(package_dir), str(deb_file)],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to build Debian package: {e.stderr}") from e
    # Print success message
    print(f"\nPackage created: {deb_file}")
    print(f"\nInstall with: sudo dpkg -i {deb_file}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create a minimal CLP core Debian package")
    parser.add_argument(
        "-b", "--build-dir", required=True, help="The CLP core CMake build directory"
    )
    parser.add_argument(
        "-v", "--version", required=True, help="The Debian package upstream version"
    )
    parser.add_argument("-r", "--revision", default="1", help="The Debian package revision")
    parser.add_argument(
        "-o", "--output-dir", default=os.getcwd(), help="The debian package output dir"
    )

    args = parser.parse_args()

    create_debian_package(args)
