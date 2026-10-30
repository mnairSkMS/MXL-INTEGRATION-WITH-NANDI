SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source $SCRIPT_DIR/_config.sh

function _call() {
    local method=$1; shift
    local endpoint=$1; shift
    local data=$1; shift

    local cmd="curl -X '$method' -H 'accept: application/json' $SERVER_URL/$endpoint"

    if [ "x$data" != "x" ]; then
        cmd="$cmd -H 'Content-Type: application/json'"
        cmd="$cmd -d $data"
    fi
echo "$cmd"
    eval "$cmd" | jq
}

function call_get() {
    local endpoint=$1; shift

    _call "GET" "$endpoint" ""
}

function call_post() {
    local endpoint=$1; shift
    local data=$1; shift
    
    _call "POST" "$endpoint" "$data"
}

function call_delete() {
    local endpoint=$1; shift
    local data=$1; shift
    
    _call "DELETE" "$endpoint" "$data"
}

function get_timestamp() {
    local offsetSeconds=$1; shift

    if [ "x$offsetSeconds" != "x" ] && [ "$offsetSeconds" != "null" ]; then
        date --utc -d "$offsetSeconds seconds" "+%Y-%m-%dT%H:%M:%S.%NZ"
    fi
}
