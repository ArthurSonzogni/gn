#!/bin/bash -eu

check=false
if [[ "$#" -ge 1 && "$1" == "--diff" ]]; then
    check=true
fi

# Check for the existance of the AUTHORS file as an easy way to determine if
# it's being run from the correct directory.
if test -f "AUTHORS"; then
    echo Building gn...
    ninja -C out gn
    echo Generating new docs/reference.md...
    content=$(out/gn help --markdown all)

    if "${check}"; then
        diff -u docs/reference.md <(echo "$content")
    else
        echo "$content" > docs/reference.md
    fi
else
    echo Please run this command from the GN checkout root directory.
    exit 1
fi
