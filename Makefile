# SPDX-License-Identifier: GPL-3.0-only
PYTHON ?= python3
BUILD_FLAGS ?=

.PHONY: all test clean
all:
	$(PYTHON) tools/build.py $(BUILD_FLAGS)

test: all
	$(PYTHON) -m unittest discover -s tests -v

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('build', ignore_errors=True)"
