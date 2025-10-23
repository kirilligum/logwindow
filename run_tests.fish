#!/usr/bin/env fish

# Test runner for logwindow

set -l EXIT_CODE 0
set -l TOTAL_TESTS 0
set -l PASSED_TESTS 0

# --- Colors ---
set -l normal (set_color normal)
set -l red (set_color red)
set -l green (set_color green)
set -l yellow (set_color yellow)

# --- Setup ---
set -l BINARY "./logwindow"
# Use mktemp to avoid race conditions and create a secure temp dir
set -l TEST_DIR (mktemp -d -t logwindow_tests.XXXXXX)

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
    echo "$red[FAIL]$normal $message"
    set -g EXIT_CODE 1
end

function pass_test
    set -l message $argv[1]
    echo "$green[PASS]$normal $message"
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
# Assuming file is logwindow.cpp as per README
clang++ -std=c++20 -Wall -Wextra -pedantic -o $BINARY logwindow.cpp
if not test $status -eq 0
    echo "$red[ERROR]$normal Compilation failed. Aborting tests."
    exit 1
end
echo "Compilation successful."

# 2. Setup test environment
echo "Test directory created at $TEST_DIR"


# --- Test Cases ---

function test_basic_truncation
    set -l test_name "Basic truncation"
    set -l log_file "$TEST_DIR/basic.log"
    set -l max_size 15

    set -l input "line 1\nline 2\nline 3\n"
    set -l expected (echo -n $input | tail -c $max_size)

    # The program waits for stdin to close before the final write
    echo -n $input | $BINARY $log_file --max-size $max_size >/dev/null 2>&1

    if not test -f $log_file
        fail_test "$test_name: Log file was not created"
        return
    end

    set -l actual (cat $log_file)
    if test "$actual" = "$expected"
        pass_test "$test_name: content matches"
    else
        fail_test "$test_name: content mismatch"
        echo "Expected: '$expected'"
        echo "Actual:   '$actual'"
    end

    set -l actual_size (string length --bytes $actual)
    if test $actual_size -eq $max_size
        pass_test "$test_name: size is correct"
    else
        fail_test "$test_name: size ($actual_size) is not equal to max_size ($max_size)"
    end
end

function test_immediate_mode
    set -l test_name "Immediate mode"
    set -l log_file "$TEST_DIR/immediate.log"
    
    # Use a named pipe to send input line-by-line
    set -l fifo_path "$TEST_DIR/immediate_fifo"
    mkfifo $fifo_path

    # Start logwindow in the background, reading from the pipe
    $BINARY $log_file --immediate < $fifo_path &
    set -l lw_pid $last_pid

    # Open the pipe for writing. The 'exec' keeps it open.
    exec 3> $fifo_path

    # Write first line and check
    echo "line 1" >&3
    sleep 0.1 # Give logwindow time to process and write
    
    set -l content1 (cat $log_file 2>/dev/null)
    set -l expected1 "line 1\n"
    if test "$content1" = "$expected1"
        pass_test "$test_name: writes first line immediately"
    else
        fail_test "$test_name: did not write first line correctly"
        echo "Expected: '$expected1'"
        echo "Actual:   '$content1'"
    end

    # Write second line and check again
    echo "line 2" >&3
    sleep 0.1

    set -l content2 (cat $log_file 2>/dev/null)
    set -l expected2 "line 1\nline 2\n"
    if test "$content2" = "$expected2"
        pass_test "$test_name: appends second line immediately"
    else
        fail_test "$test_name: did not append second line correctly"
        echo "Expected: '$expected2'"
        echo "Actual:   '$content2'"
    end

    # Close the pipe, which will cause logwindow to exit
    exec 3>&-
    wait $lw_pid 2>/dev/null
end

function test_debounced_write
    set -l test_name "Debounced write"
    set -l log_file "$TEST_DIR/debounced.log"
    set -l write_interval_ms 200
    set -l write_interval_s (math $write_interval_ms / 1000)

    set -l fifo_path "$TEST_DIR/debounced_fifo"
    mkfifo $fifo_path
    
    $BINARY $log_file --write-interval $write_interval_ms < $fifo_path &
    set -l lw_pid $last_pid
    exec 3> $fifo_path

    echo "line 1" >&3
    # Wait for LESS than the interval
    sleep (math $write_interval_s / 2)

    if test -f $log_file
        fail_test "$test_name: file was created before interval expired"
    else
        pass_test "$test_name: file is not created before interval"
    end
    
    # Wait for MORE than the interval total
    sleep (math $write_interval_s)

    set -l content_after (cat $log_file 2>/dev/null)
    set -l expected_after "line 1\n"
    if test "$content_after" = "$expected_after"
        pass_test "$test_name: file is written after interval"
    else
        fail_test "$test_name: file content is wrong after interval"
        echo "Expected: '$expected_after'"
        echo "Actual:   '$content_after'"
    end

    exec 3>&-
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
    echo "$green[SUCCESS]$normal All $TOTAL_TESTS tests passed."
else
    set -l failed_tests (math $TOTAL_TESTS - $PASSED_TESTS)
    echo "$red[FAILURE]$normal $failed_tests out of $TOTAL_TESTS tests failed."
end

exit $EXIT_CODE
