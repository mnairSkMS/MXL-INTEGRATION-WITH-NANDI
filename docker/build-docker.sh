#!/usr/bin/env bash

CMD_DOCKER=docker

# <directory>,<docker image name>,<docker target>,<docker context path>
# Where:
#  <docker context path> - image root path if empty
#IMAGES="\
#        utpl-base,utpl/base,, \
#        utpl-devel,utpl/devel,, \
#        utpl-gstreamer-test,utpl/gstreamer-test,gstreamer-test,../transcoder \
#"

IMAGES="\
        utpl-base,utpl/base,base,../ \
        utpl-devel,utpl/devel,,../ \
        utpl-transcoder,utpl/transcoder,transcoder,../ \
        utpl-transcoder-test,utpl/transcoder-test,,../ \
        utpl-license-toolkit,utpl/license-toolkit,,../ \
"

function log_info() {
        echo -e "## INFO: $@"
}


function docker_check() {
        ${CMD_DOCKER} images | grep $1 > /dev/null 2>&1
        return $?
}


function docker_build() {
        local dir=$1; shift
        local name=$1; shift
        local target=$1; shift
        local contextDir=$1; shift
        
        dir=$(realpath $dir)

        if [ -z "$contextDir" ]; then
                contextDir=${dir}
        fi
        contextDir=$(realpath $contextDir)

        if [ ! -e ${dir} ]; then
                log_info "Directory $dir does not exist!"
        else
                local cmd="${CMD_DOCKER} build -f ${dir}/Dockerfile -t ${name}"
                
                if [ "x${target}" != "x" ]; then
                        cmd="${cmd} --target ${target}"
                fi
        
                cmd="${cmd} --progress=plain ${contextDir}"
        
                log_info "Building ${name} from ${dir}"
                
                # echo ${cmd}
                eval ${cmd} || exit 1
        fi
}

for i in ${IMAGES}; do
        IFS=',' read -a array <<< "$i"
        docker_build "${array[0]}" "${array[1]}" "${array[2]}" "${array[3]}"
done
