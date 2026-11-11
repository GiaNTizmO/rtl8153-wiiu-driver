#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_framing.c driver/rtl8153_framing.c -o build/tests/test_framing
ASAN_OPTIONS=detect_leaks=1 build/tests/test_framing
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_chip.c -o build/tests/test_chip
ASAN_OPTIONS=detect_leaks=1 build/tests/test_chip
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_transport.c driver/rtl8153_framing.c -o build/tests/test_transport
ASAN_OPTIONS=detect_leaks=1 build/tests/test_transport
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_patch.c -o build/tests/test_patch
ASAN_OPTIONS=detect_leaks=1 build/tests/test_patch
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_native_frames.c native/frame_adapter.c driver/rtl8153_framing.c -o build/tests/test_native_frames
ASAN_OPTIONS=detect_leaks=1 build/tests/test_native_frames
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_ios_core.c native/rtl8153_ios_core.c -o build/tests/test_ios_core
ASAN_OPTIONS=detect_leaks=1 build/tests/test_ios_core

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_batch_bridge.c native/batch_bridge.c native/frame_adapter.c driver/rtl8153_framing.c -o build/tests/test_batch_bridge
ASAN_OPTIONS=detect_leaks=1 build/tests/test_batch_bridge

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_native_uhs.c native/uhs_transport.c -o build/tests/test_native_uhs
ASAN_OPTIONS=detect_leaks=1 build/tests/test_native_uhs

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_completion_bridge.c native/completion_bridge.c -o build/tests/test_completion_bridge
ASAN_OPTIONS=detect_leaks=1 build/tests/test_completion_bridge

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_ios_completion_adapter.c native/ios_completion_adapter.c \
    native/completion_bridge.c -o build/tests/test_ios_completion_adapter
ASAN_OPTIONS=detect_leaks=1 build/tests/test_ios_completion_adapter

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_completion_tokens.c native/completion_tokens.c native/uhs_transport.c -o build/tests/test_completion_tokens
ASAN_OPTIONS=detect_leaks=1 build/tests/test_completion_tokens

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_firmware_rx.c native/firmware_rx.c native/completion_tokens.c native/uhs_transport.c \
    native/batch_bridge.c native/frame_adapter.c driver/rtl8153_framing.c -o build/tests/test_firmware_rx
ASAN_OPTIONS=detect_leaks=1 build/tests/test_firmware_rx

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_firmware_tx.c native/firmware_tx.c native/completion_tokens.c native/uhs_transport.c \
    native/batch_bridge.c native/frame_adapter.c driver/rtl8153_framing.c -o build/tests/test_firmware_tx
ASAN_OPTIONS=detect_leaks=1 build/tests/test_firmware_tx

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Itests/heartbeat -Ivendor/lwip/src/include \
    tests/test_heartbeat.c net/port/net_heartbeat.c vendor/lwip/src/core/timeouts.c -o build/tests/test_heartbeat
ASAN_OPTIONS=detect_leaks=1 build/tests/test_heartbeat

gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_owner.c native/transfer_owner.c native/uhs_transport.c -o build/tests/test_owner
ASAN_OPTIONS=detect_leaks=1 build/tests/test_owner
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/test_ops_install.c native/ops_table.c native/discovery_guard.c -o build/tests/test_ops_install
ASAN_OPTIONS=detect_leaks=1 build/tests/test_ops_install
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/test_loader_contract.c native/loader_contract.c -o build/tests/test_loader_contract
ASAN_OPTIONS=detect_leaks=1 build/tests/test_loader_contract
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_native_glue.c native/native_glue.c native/rtl8153_ios_core.c \
    native/uhs_transport.c native/transfer_owner.c native/completion_tokens.c native/batch_bridge.c \
    native/frame_adapter.c driver/rtl8153_framing.c -o build/tests/test_native_glue
ASAN_OPTIONS=detect_leaks=1 build/tests/test_native_glue
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/test_ios_profile.c native/ios_profile.c -o build/tests/test_ios_profile
ASAN_OPTIONS=detect_leaks=1 build/tests/test_ios_profile
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/test_stroopwafel_stage.c native/stroopwafel_stage.c native/loader_contract.c native/ios_profile.c -o build/tests/test_stroopwafel_stage
ASAN_OPTIONS=detect_leaks=1 build/tests/test_stroopwafel_stage
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    tests/test_mocha_alloc_probe.c native/mocha_alloc_probe.c -o build/tests/test_mocha_alloc_probe
ASAN_OPTIONS=detect_leaks=1 build/tests/test_mocha_alloc_probe
