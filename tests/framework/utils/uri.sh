################################
#### URI extraction utils
################################
function utils_uri_get_proto() {
    local uri=$1; shift
    local toLower=$1; shift

    local proto=$(echo "$uri" | grep "://" | sed -e's,^\(.*://\).*,\1,g')

    if [ $toLower -eq 1 ]; then
        echo "$proto" | tr '[:upper:]' '[:lower:]'
    else
        echo "$proto"
    fi
}

function utils_uri_get_uri_wo_proto() {
    local uri=$1; shift
    local proto=$(utils_uri_get_proto "$uri" 0)
    
    echo "${uri/$proto/}"
}

function utils_uri_get_userpass() {
    local uri=$1; shift

    echo "$(utils_uri_get_uri_wo_proto $uri)" | grep "@" | cut -d"/" -f1 | rev | cut -d"@" -f2- | rev
}

function utils_uri_get_pass() {
    local uri=$1; shift

    echo "$(utils_uri_get_userpass $uri)" | grep ":" | cut -d":" -f2
}

function utils_uri_get_user() {
    local uri=$1; shift

    local userpass=$(utils_uri_get_userpass $uri)
    local pass=$(utils_uri_get_pass $uri)

    if [ -n "$pass" ]; then
        echo "$userpass" | grep ":" | cut -d":" -f1
    else
        echo "$userpass"
    fi
}

function utils_uri_get_hostport() {
    local uri=$1; shift

    local urlNoProtocol=$(utils_uri_get_uri_wo_proto $uri)
    local userPass=$(utils_uri_get_userpass $uri)

    echo "${urlNoProtocol/$userPass@/}" | cut -d"/" -f1
}

function utils_uri_get_host() {
    local uri=$1; shift

    echo "$(utils_uri_get_hostport $uri)" | cut -d":" -f1
}

function utils_uri_get_port() {
    local uri=$1; shift

    echo "$(utils_uri_get_hostport $uri)" | grep ":" | cut -d":" -f2
}

function utils_uri_get_path() {
    local uri=$1; shift

    echo "$(utils_uri_get_uri_wo_proto $uri)" | grep "/" | cut -d"/" -f1-
}

function utils_uri_proto_is_udp() {
    local uri=$1

    if [ "$(utils_uri_get_proto $uri 1)" = "udp://" ]; then
        return 0
    fi

    return 1
}

function utils_uri_proto_is_srt() {
    local uri=$1

    if [ "$(utils_uri_get_proto $uri 1)" = "srt://" ]; then
        return 0
    fi

    return 1
}

function utils_uri_proto_is_file() {
    local uri=$1

    case "$(utils_uri_get_proto $uri 1)" in
        "file://"|"hls+file://") return 0 ;;
    esac
    
    return 1
}

function utils_uri_proto_is_http() {
    local uri=$1

    if [ "$(utils_uri_get_proto $uri 1)" = "http://" ]; then
        return 0
    fi

    return 1
}

function utils_uri_proto_is_hls() {
    local uri=$1
    case "$(utils_uri_get_proto $uri 1)" in
        "http://"|"https://"|"hls+file://") return 0 ;;
    esac
    return 1
}
