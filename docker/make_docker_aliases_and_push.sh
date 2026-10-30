#!/usr/bin/env bash

set -e

VERSION=${VERSION:-$(git rev-parse --short HEAD)}

IMAGES="base devel transcoder license-toolkit"

function log_info() {
        echo -e "## INFO: $@"
}

function docker_tag() {
	local src=$1; shift
	local dst=$1; shift

	echo "Tagging '$src' as '$dst'"

	docker tag "$src" "$dst"
}

function docker_push() {
	local name=$1; shift
	
	echo "Pushing image '$name'"

	docker push "$name"
}

function make_alias() {
	local name=$1; shift
	local tag=$1; shift
	local markLatest=$1; shift

	log_info "Creating a remote alias of image \"$name\" with tag: \"${tag}\""
	
	#docker_tag "utpl/${name}:latest" "ghcr.io/skandha-media-services/utpl/${name}:${tag}"
	docker_tag "utpl/${name}:latest" "ghcr.io/skandha-media-services/skandha-transcoder/${name}:${tag}"

	if [ $markLatest -eq 1 ]; then
		#docker_tag "ghcr.io/skandha-media-services/utpl/${name}:${tag}" "ghcr.io/skandha-media-services/utpl/${name}:latest"
		docker_tag "ghcr.io/skandha-media-services/utpl/${name}:${tag}" "ghcr.io/skandha-media-services/skandha-transcoder/${name}:latest"
	fi
}

function push_image() {
	local name=$1; shift
	local tag=$1; shift
	local markLatest=$1; shift
	
	log_info "Pushing \"${tag}\" and latest \"${name}\" images to remote hub"
	
	#docker_push "ghcr.io/skandha-media-services/utpl/${name}:${tag}"
	docker_push "ghcr.io/skandha-media-services/skandha-transcoder/${name}:${tag}"

	if [ $markLatest -eq 1 ]; then
		#docker_push "ghcr.io/skandha-media-services/utpl/${name}:latest"
		docker_push "ghcr.io/skandha-media-services/skandha-transcoder/${name}:latest"
	fi
}

MARK_AS_LATEST=0

OPTSTRING="hl"
while getopts ${OPTSTRING} opt; do
    case ${opt} in
        h)
            help
            ;;
        l)
            MARK_AS_LATEST=1
            ;;
        :)
            echo "Error: Option -${OPTARG} requires an argument"
            exit 1
            ;;
        ?)
            echo "Invalid option: -${OPTARG}"
            exit 1
            ;;
    esac
done

for image in ${IMAGES}; do
	make_alias "${image}" ${VERSION} ${MARK_AS_LATEST}
	push_image "${image}" ${VERSION} ${MARK_AS_LATEST}
done
