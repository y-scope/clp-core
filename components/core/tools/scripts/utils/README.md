This directory contains uncategorized utility scripts.


* `build-and-run-unit-tests.sh` can be used to build all executables and run
  the unit tests.
* `run-in-container.sh` can be used to run a command (e.g., `make`), in a 
  container containing the core component's dependencies.
  * All commands are run from the root of the core component.
  * This is useful if, for instance, you're developing on Ubuntu 18.04 but the
    package uses Ubuntu 20.04; you can build in the Ubuntu 20.04 container to
    avoid compatibility issues.
* `create-debian-package.py` can be used to generate a minimal Debian package
  containing CLP core package. Sample command:
  `python3 create-debian-package.py --version 0.0 --revision 1 --build-dir ../../../build`
