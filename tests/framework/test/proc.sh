function test_proc_kill_and_wait() {
    local pid=
    local signal="SIGTERM"

    local OPTIND
    while getopts ":p:s:" opt; do
        case "$opt" in
            p)
                pid="$OPTARG"
            ;;
            s)
                signal="$OPTARG"
            ;;
        esac
    done 
    shift $((OPTIND-1))

    if [ -n "$pid" ]; then
        case $(pkill -$signal -P $pid) in
            1)
                test_log "One or more processes matched the parent PID of $pid"

                pidwait -P $pid
                ;;
            2)
                test_log "No process matched"
                ;;
            3)
                test_log "Syntax error"
                ;;
            4)
                test_err "Fatal error: out of memory etc"
                ;;
        esac
    else
        test_err "Process ID was not specified!"
    fi
}

function test_proc_call_async() {
    read -r pid < <({
        log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m ""
        log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m "###############################################"
        log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m "Executing: $*"
        log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m "###############################################"
        log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m ""
        (eval "$@") >> "$TEST_PATH_OUT_CMD_LOG" 2>&1 & 
        echo $!
    })

    echo "$pid"
}

function test_proc_call() {
    log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m ""
    log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m "###############################################"
    log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m "Executing: $*"
    log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m "###############################################"
    log_dbg -o "$TEST_PATH_OUT_CMD_LOG" -m ""

    (eval "$@") >> "$TEST_PATH_OUT_CMD_LOG" 2>&1 || ( test_err "$* exited with $?"; return 1 )

    return 0
}
