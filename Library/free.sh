#!/bin/bash

# /usr/bin \

for dir in \
    /var/jb/Applications \
    /var/jb/Library/Wallpaper \
    /var/jb/Library/Ringtones \
    /var/jb/usr/include \
    /var/jb/usr/share \
; do
    . /var/jb/usr/libexec/cydia/move.sh "$@" "${dir}"
done

sync
