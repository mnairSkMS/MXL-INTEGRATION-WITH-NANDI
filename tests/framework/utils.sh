function utils_uri_is_listener_mode() {
    local uri=$1

    if utils_uri_proto_is_udp $uri; then
        return 0

    elif utils_uri_proto_is_srt $uri; then
        local path=$(utils_uri_get_path $uri)

        if echo $path | grep "mode=listener" > /dev/null; then
            return 0
        else
            return 1
        fi
    fi

    return 1
}

function test_uri_download() {
    local srcUri=$1; shift
    local dstUri=$1; shift

    test_trace "Downloading from \"$srcUri\" to \"$dstUri\""

    if ! test_proc_call wget --no-verbose ${srcUri} -O ${dstUri}; then
        rm -f ${dstUri}
        return 1
    fi

    return 0
}

function utils_media_meta_save() {
    local inFile=$1; shift
    local outFile=$1; shift

    test_proc_call "ffprobe  -show_streams  -show_format  ${inFile} -of json > ${outFile}"
}

function utils_media_scte_save() {
    local inFile=$1; shift
    local outFile=$1; shift
    local pid=$1; shift
    local proto=${1:-"file"}

    local pidArg=""

    if [ "x$pid" != "x" ]; then
        pidArg="-s $pid"
    fi

    test_proc_call "tsp -I ${proto} ${inFile} -P splicemonitor -a -d -j $pidArg -O drop > ${outFile}"
}

function utils_udp_port_wait() {
    local port=$1; shift 
    local timeout=$1; shift

    local sleepTime=0;
    local sleepInterval=0.1

    test_log "Waiting for the UDP port $port to be opened"

    while ! $(netstat -tuplen 2>&1 | grep ":$port " > /dev/null); do
        if [ 1 -eq $(bc <<< "$sleepTime >= ${timeout}") ]; then
            test_err "Waiting for UDP port $port to be opened has timed out! $sleepTime >= $timeout"
            return 1
        fi

        sleep $sleepInterval

        sleepTime=$(bc <<< "$sleepTime + $sleepInterval")
    done

    return 0
}

function utils_http_file_wait() {
    local url=$1; shift
    local timeout=$1; shift

    local sleepTime=0;
    local sleepInterval=0.1

    test_log "Waiting for the HTTP file $url to be available"

    while ! $(wget --spider --quiet $url 2>&1); do
        if [ 1 -eq $(bc <<< "$sleepTime >= ${timeout}") ]; then
            test_err "Waiting for HTTP file $url to be available has timed out! $sleepTime >= $timeout"
            return 1
        fi

        sleep $sleepInterval

        sleepTime=$(bc <<< "$sleepTime + $sleepInterval")
    done

    return 0
}