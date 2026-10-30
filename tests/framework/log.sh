function _log() {
    local logLevel=$1; shift
    
    local targetFile=/dev/stdout
    local msg=""
    local forceOutput=0

    local OPTIND
    while getopts ":m:o:f" opt; do
        case "$opt" in
            m)
                msg="$OPTARG"
            ;;
            o)
                targetFile="$OPTARG"
            ;;
            f)
                forceOutput=1
            ;;
        esac
    done 
    shift $((OPTIND-1))

    msg="[ $(date +%F\ %T) ] [${logLevel}] $msg"

    if [ $forceOutput -eq 1 ]; then
        echo -e "$msg" | tee -a $targetFile
    else
        echo -e "$msg" >> $targetFile
    fi
}

function log_trace() {
    _log "TRC" "$@"
}

function log_dbg() {
    _log "DBG" "$@"
}

function log_log() {
    _log "LOG" "$@"
}

function log_warn() {
    _log "WRN" "$@"
}

function log_err() {
    _log "ERR" "$@"
}