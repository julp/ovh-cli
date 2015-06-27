#!/bin/bash

declare -r KEY_NAME=default
declare -r OUTPUT_PATH=/tmp

OLD_UMASK=$(umask)
umask 0377
ssh-keygen -t rsa -f "${OUTPUT_PATH}/id_rsa"
KEY_AS_STRING=$(cat "${OUTPUT_PATH}/id_rsa.pub")

ovh <<EOS
key "${KEY_NAME}" add "${KEY_AS_STRING}"

key "${KEY_NAME}" default on
EOS

umask "${OLD_UMASK}"
