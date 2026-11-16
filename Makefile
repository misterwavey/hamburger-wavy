# SPDX-License-Identifier: GPL-3.0-only
PYTHON ?= python3
BUILD_FLAGS ?=

.PHONY: all profile flash-probe test clean
all:
	$(PYTHON) tools/build.py $(BUILD_FLAGS)

profile:
	$(PYTHON) tools/build.py --profile $(BUILD_FLAGS)

flash-probe:
	$(PYTHON) tools/build.py --flash-probe $(BUILD_FLAGS)

test: all
	$(PYTHON) -m unittest discover -s tests -v

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('build', ignore_errors=True)"
