function utils_json_array_length() {
    local jsFile=$1; shift
    local path=$1; shift

    if [ -e "$jsFile" ]; then
        jq -r "$path | length" $jsFile
    else
        echo "$jsFile" | jq -r "$path | length"
    fi
}

function utils_json_value_get() {
    local jsFile=$1; shift
    local path=$1; shift

    if [ -e "$jsFile" ]; then
        jq -r "$path // empty" $jsFile
    else
        echo "$jsFile" | jq -r "$path // empty"
    fi
}

function utils_json_key_count() {
    local jsFile=$1; shift
    local path=$1; shift
    local index=$1; shift

    if [ -e "$jsFile" ]; then
        jq -r "$path | keys | length" $jsFile
    else
        echo "$jsFile" | jq -r "$path | keys | length"
    fi
}

function utils_json_key_get() {
    local jsFile=$1; shift
    local path=$1; shift
    local index=$1; shift

    if [ -e "$jsFile" ]; then
        jq -r "$path | keys[$index]" $jsFile
    else
        echo "$jsFile" | jq -r "$path | keys[$index]"
    fi
}

function utils_json_key_has() {
    local jsFile=$1; shift
    local path=$1; shift
    local node=$1; shift

    if [ -e "$jsFile" ]; then
        jq -e "$path | has(\"$node\")" $jsFile 1>&2 > /dev/null || return 1

    else
        echo "$jsFile" | jq -e "$path | has(\"$node\")" 1>&2 > /dev/null || return 1
    fi

    return 0
}
