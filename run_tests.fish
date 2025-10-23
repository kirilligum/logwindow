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
# Assuming file is main.cc
clang++ -std=c++20 -Wall -Wextra -pedantic -o $BINARY main.cc
if not test $status -eq 0
    echo "$red[ERROR]$normal Compilation failed. Aborting tests."
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
    set -l input (printf "line 1\nline 2\nline 3\n")
    set -l expected (printf "line 2\nline 3\n")
    set -l expected_size (echo -n -- "$expected" | wc -c)

    # The program waits for stdin to close before the final write
    printf -- "$input" | $BINARY $log_file --max-size $max_size >/dev/null 2>&1

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

    set -l actual_size (echo -n -- "$actual" | wc -c)
    if test "$actual_size" -eq "$expected_size"
        pass_test "$test_name: size is correct"
    else
        fail_test "$test_name: size ($actual_size) is not equal to expected size ($expected_size)"
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

    # Write to the pipe in a separate process. It will close the pipe when done.
    begin
        echo "line 1"
        sleep 0.2 # Give writer time to send second line
        echo "line 2"
    end > $fifo_path &

    # Give logwindow time to process the first line
    sleep 0.1
    
    set -l content1 (cat $log_file 2>/dev/null)
    set -l expected1 (printf "line 1\n")
    if test "$content1" = "$expected1"
        pass_test "$test_name: writes first line immediately"
    else
        fail_test "$test_name: did not write first line correctly"
        echo "Expected: '$expected1'"
        echo "Actual:   '$content1'"
    end

    # Give logwindow time to process the second line
    sleep 0.2

    set -l content2 (cat $log_file 2>/dev/null)
    set -l expected2 (printf "line 1\nline 2\n")
    if test "$content2" = "$expected2"
        pass_test "$test_name: appends second line immediately"
    else
        fail_test "$test_name: did not append second line correctly"
        echo "Expected: '$expected2'"
        echo "Actual:   '$content2'"
    end

    # Wait for logwindow to finish (it exits when the writer closes the pipe)
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
    
    # Keep the pipe open in the background to test debouncing.
    # We will kill this process to signal EOF later.
    sleep 300 > $fifo_path &
    set -l pipe_holder_pid $last_pid

    # Write one line to the pipe
    echo "line 1" > $fifo_path
    
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
    set -l expected_after (printf "line 1\n")
    if test "$content_after" = "$expected_after"
        pass_test "$test_name: file is written after interval"
    else
        fail_test "$test_name: file content is wrong after interval"
        echo "Expected: '$expected_after'"
        echo "Actual:   '$content_after'"
    end

    # Clean up and signal EOF by killing the process holding the pipe open
    kill $pipe_holder_pid
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
