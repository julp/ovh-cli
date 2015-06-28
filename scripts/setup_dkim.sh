#!/bin/bash

declare -r DOMAIN_NAME="domain.tld"
declare -r DKIM_RECORD_NAME="selector1._domainkey"
declare -r OUTPUT_PATH=/tmp

OLD_UMASK=$(umask)
umask 0377

openssl genrsa -out ${OUTPUT_PATH}/dkimproxy.private.key 1024
openssl rsa -in ${OUTPUT_PATH}/dkimproxy.private.key -out ${OUTPUT_PATH}/dkimproxy.public.key -pubout -outform PEM

# chmod 0600 ${OUTPUT_PATH}/*.key
# chown dkimproxy ${OUTPUT_PATH}/*.key

declare -r PUBKEY_CONTENT=$(cat ${OUTPUT_PATH}/dkimproxy.public.key | sed '/^-/d' | awk '{printf "%s", $1}')

ovh --no-confirm --yes <<-EOS
    domain ${DOMAIN_NAME} record ${DKIM_RECORD_NAME} delete
    domain ${DOMAIN_NAME} record ${DKIM_RECORD_NAME} add "g=*; k=rsa; p=${PUBKEY_CONTENT};" type TXT
EOS

umask "${OLD_UMASK}"
