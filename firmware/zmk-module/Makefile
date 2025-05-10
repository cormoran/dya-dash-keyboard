MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
REPO_ROOT := $(shell git rev-parse --show-superproject-working-tree --show-toplevel | head -1)

dev-container-init:
	git submodule update --init --recursive
	devcontainer up --workspace-folder $(REPO_ROOT)/firmware/zmk-module
	# devcontainer exec --workspace-folder $(REPO_ROOT)/firmware/zmk-module bash -c "cd /workspaces/dya-dash-keyboard/firmware/zmk_firmware && west init -l app"
	devcontainer exec --workspace-folder $(REPO_ROOT)/firmware/zmk-module bash -c "cd /workspaces/dya-dash-keyboard/firmware/zmk-module && make init"

dev-container-bash:
	devcontainer exec --workspace-folder $(REPO_ROOT)/firmware/zmk-module bash

dev-container-build-dya-dash:
	devcontainer exec --workspace-folder $(REPO_ROOT)/firmware/zmk-module bash -c "cd /workspaces/dya-dash-keyboard/firmware/zmk-module && make build-dya-dash"

init:
	git submodule update --init --recursive
	cd $(REPO_ROOT)/firmware/zmk_firmware && west init -l app && west update

build-dya-dash:
	make build-dya-dash-right
	make build-dya-dash-left

build-dya-dash-right:
	cd $(REPO_ROOT)/firmware/zmk-module && bash build_dya_dash.sh right -s

build-dya-dash-left:
	cd $(REPO_ROOT)/firmware/zmk-module && bash build_dya_dash.sh left