#!/bin/bash

declare -r __DIR__=$(dirname $(readlink -f "${BASH_SOURCE}"))

declare -r POT_FILE="${__DIR__}/i18n/ovh-cli.pot"

xgettext -L C --keyword=_ --keyword=N_ --output=${POT_FILE} ${__DIR__}/*.c ${__DIR__}/**/*.c

find ${__DIR__} -type f -name "*.po" -exec msgmerge -U {} ${POT_FILE} \;
