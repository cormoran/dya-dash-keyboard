REPO_ROOT=$(git rev-parse --show-superproject-working-tree --show-toplevel | head -1)
BUILD_DIR=$REPO_ROOT/firmware/zmk-module/build

ZMK_EXTRA_MODULES="$REPO_ROOT/firmware/zmk-module/$(basename $(dirname $0))"
cd $REPO_ROOT/firmware/zmk_firmware/app
west build -d ${BUILD_DIR}/test --pristine --board native_posix_64 -- -DZMK_EXTRA_MODULES="$ZMK_EXTRA_MODULES"
