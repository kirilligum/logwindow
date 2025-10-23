#!/usr/bin/env fish

# Test runner for logwindow

set -g EXIT_CODE 0
set -g TOTAL_TESTS 0
set -g PASSED_TESTS 0

# --- Colors ---
set -g normal (set_color normal)
set -g red (set_color red)
set -g green (set_color green)
set -g yellow (set_color yellow)

# --- Setup ---
set -g BINARY "./logwindow"
# Use mktemp to avoid race conditions and create a secure temp dir
set -g TEST_DIR (mktemp -d -t logwindow_tests.XXXXXX)

function cleanup
    echo ""
    echo "Cleaning up..."
    # Gracefully stop background processes
    jobs -p | xargs -r kill
    rm -rf "$TEST_DIR"
end

# Set up a trap to run cleanup on script exit
trap cleanup EXIT

function fail_test
    set -l message $argv[1]
    echo "{$red}[FAIL]{$normal} $message"
    set -g EXIT_CODE 1
end

function pass_test
    set -l message $argv[1]
    echo "{$green}[PASS]{$normal} $message"
    set -g PASSED_TESTS (math $PASSED_TESTS + 1)
end

function run_test
    set -l name $argv[1]
    set -l cmd $argv[2..-1]
    set -g TOTAL_TESTS (math $TOTAL_TESTS + 1)
    echo ""
    echo "$yellow--- Running test: $name ---$normal"
    eval $cmd
end

# --- Main ---
echo "Starting logwindow tests..."

# 1. Compile the binary
echo "Compiling..."
# Assuming file is main.cc
clang++ -std=c++20 -Wall -Wextra -pedantic -o $BINARY main.cc
if not test $status -eq 0
    echo "{$red}[ERROR]{$normal} Compilation failed. Aborting tests."
    exit 1
end
echo "Compilation successful."

# 2. Setup test environment
echo "Test directory created at $TEST_DIR"


# --- Test Cases ---

function test_basic_truncation
    set -l test_name "Basic truncation (line-based)"
    set -l log_file "$TEST_DIR/basic.log"
    set -l max_size 15

    # With line-based truncation, "line 1\n" should be dropped.
    set -l expected (printf "line 2\nline 3\n" | string collect)
    set -l expected_size (string length -- "$expected")

    # The program waits for stdin to close before the final write
    printf "line 1\nline 2\nline 3\n" | $BINARY $log_file --max-size $max_size >/dev/null 2>&1

    if not test -f $log_file
        fail_test "$test_name: Log file was not created"
        return
    end

    set -l actual (cat $log_file | string collect)
    if test "$actual" = "$expected"
        pass_test "$test_name: content matches"
    else
        fail_test "$test_name: content mismatch"
        # Use printf with %q to show escaped version for debugging
        printf "Expected length: %d\n" (string length -- "$expected")
        printf "Actual length:   %d\n" (string length -- "$actual")
    end

    set -l actual_size (string length -- "$actual")
    if test "$actual_size" -eq "$expected_size"
        pass_test "$test_name: size is correct"
    else
        fail_test "$test_name: size ($actual_size) is not equal to expected size ($expected_size)"
    end
end

function test_immediate_mode
    set -l test_name "Immediate mode"
    set -l log_file "$TEST_DIR/immediate.log"

    # Create a writer script
    set -l writer_script "$TEST_DIR/writer.sh"
    echo "#!/bin/bash" > $writer_script
    echo "echo 'line 1'" >> $writer_script
    echo "sleep 0.15" >> $writer_script
    echo "echo 'line 2'" >> $writer_script
    chmod +x $writer_script

    # Run the test using the writer script
    $writer_script | $BINARY $log_file --immediate &
    set -l lw_pid $last_pid

    # Give logwindow time to process the first line
    sleep 0.1

    set -l content1 (cat $log_file 2>/dev/null | string collect)
    set -l expected1 (printf "line 1\n" | string collect)
    if test "$content1" = "$expected1"
        pass_test "$test_name: writes first line immediately"
    else
        fail_test "$test_name: did not write first line correctly"
        printf "Expected length: %d\n" (string length -- "$expected1")
        printf "Actual length:   %d\n" (string length -- "$content1")
    end

    # Give logwindow time to process the second line
    sleep 0.2

    set -l content2 (cat $log_file 2>/dev/null | string collect)
    set -l expected2 (printf "line 1\nline 2\n" | string collect)
    if test "$content2" = "$expected2"
        pass_test "$test_name: appends second line immediately"
    else
        fail_test "$test_name: did not append second line correctly"
        printf "Expected length: %d\n" (string length -- "$expected2")
        printf "Actual length:   %d\n" (string length -- "$content2")
    end

    # Wait for logwindow to finish
    wait $lw_pid 2>/dev/null
end

function test_debounced_write
    set -l test_name "Debounced write"
    set -l log_file "$TEST_DIR/debounced.log"
    set -l write_interval_ms 200
    set -l write_interval_s (math $write_interval_ms / 1000)

    # Create a writer script that writes two lines with a gap to trigger time checks
    set -l writer_script "$TEST_DIR/writer_debounced.sh"
    echo "#!/bin/bash" > $writer_script
    echo "echo 'line 1'" >> $writer_script
    echo "sleep 0.25" >> $writer_script  # Sleep longer than write interval
    echo "echo 'line 2'" >> $writer_script  # This triggers the time check
    chmod +x $writer_script

    # Run the test
    $writer_script | $BINARY $log_file --write-interval $write_interval_ms &
    set -l lw_pid $last_pid

    # Wait for LESS than the interval
    sleep (math $write_interval_s / 2)

    if test -f $log_file
        fail_test "$test_name: file was created before interval expired"
    else
        pass_test "$test_name: file is not created before interval"
    end

    # Wait for the second line to arrive and trigger the write
    sleep 0.3

    set -l content_after (cat $log_file 2>/dev/null | string collect)
    set -l expected_after (printf "line 1\nline 2\n" | string collect)
    if test "$content_after" = "$expected_after"
        pass_test "$test_name: file is written after interval on next input"
    else
        fail_test "$test_name: file content is wrong after interval"
        printf "Expected length: %d\n" (string length -- "$expected_after")
        printf "Actual length:   %d\n" (string length -- "$content_after")
    end

    # Wait for logwindow to finish
    wait $lw_pid 2>/dev/null
end

# --- Run tests ---
run_test "Basic Truncation" test_basic_truncation
run_test "Immediate Mode" test_immediate_mode
run_test "Debounced Write" test_debounced_write

# --- Summary ---
echo ""
echo "--- Test Summary ---"
if test $EXIT_CODE -eq 0
    echo "{$green}[SUCCESS]{$normal} All $TOTAL_TESTS tests passed."
else
    set -l failed_tests (math $TOTAL_TESTS - $PASSED_TESTS)
    echo "{$red}[FAILURE]{$normal} $failed_tests out of $TOTAL_TESTS tests failed."
end

exit $EXIT_CODE
