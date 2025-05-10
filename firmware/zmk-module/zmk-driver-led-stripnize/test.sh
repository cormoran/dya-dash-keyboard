# devcontainer exec --workspace-folder <path to zmk-module> bash zmk-driver-led-stripnize/test.sh
REPO_ROOT=$(git rev-parse --show-superproject-working-tree --show-toplevel | head -1)

export ZMK_SRC_DIR=$REPO_ROOT/firmware/zmk_firmware/app
export ZMK_BUILD_DIR=$REPO_ROOT/firmware/zmk-module/build/zmk-driver-led-stripnize
export ZMK_EXTRA_MODULES=$REPO_ROOT/firmware/zmk-module/zmk-driver-led-stripnize
# export ZMK_TESTS_VERBOSE=1
TEST_DIR=$REPO_ROOT/firmware/zmk-module/zmk-driver-led-stripnize/tests

cd $ZMK_SRC_DIR
west test $TEST_DIR
