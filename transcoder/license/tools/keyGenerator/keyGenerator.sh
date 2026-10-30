#!/usr/bin/env bash

set -e

function help() {
    echo "Usage: $(basename $0) [-o output_dir] [-h]"
    echo "  -o - path to output directory"
    echo "  -h - shows this help"
    exit 1
}

DIR_OUT=$(pwd)
VERBOSE=0

OPTSTRING="o:hv"
while getopts ${OPTSTRING} opt; do
    case ${opt} in
        h)
            help
            ;;
        o)
            DIR_OUT=${OPTARG}
            ;;
        v)
            VERBOSE=1
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

if [ -f ${DIR_OUT} ]; then
    echo "Output directory '${DIR_OUT}' points to a regular file - Please verify output directory"
    exit 1
fi

mkdir -p ${DIR_OUT}

KEY_PREFIX=license
KEY_TYPE=ed25519

LIC_KEY_PRIV=${DIR_OUT}/${KEY_PREFIX}_ed25519_private_key.der
LIC_KEY_PUB=${DIR_OUT}/${KEY_PREFIX}_ed25519_public_key.der
LIC_KEY_AES=${DIR_OUT}/${KEY_PREFIX}_encryption_key.bin

openssl genpkey -algorithm ${KEY_TYPE} -outform DER -out ${LIC_KEY_PRIV}
openssl pkey -in ${LIC_KEY_PRIV} -inform DER -pubout -outform DER -out ${LIC_KEY_PUB}

openssl rand -out ${LIC_KEY_AES} 32

echo "The license keys have been successfully stored in the directory: ${DIR_OUT}"
find ${DIR_OUT} -maxdepth 1 -type f -iname "${KEY_PREFIX}_*" -printf "    %f\n"

if [ $VERBOSE -eq 1 ]; then
    echo -e "\n####################\nGenerated ${KEY_TYPE} keys:\n####################\n"
    openssl pkey -in ${LIC_KEY_PRIV} -inform DER -text -noout
    # openssl pkey -in ${LIC_KEY_PUB} -inform DER -pubin -text -noout
    echo -e "\n\n####################\nGenerated AES key:\n####################\n"
    cat ${LIC_KEY_AES} | xxd  -R never -c 16 -g 1 -ps
    echo ""
fi