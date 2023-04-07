#!/bin/bash

shopt -s extglob nullglob

if [[ ${1:0:1} == - ]]; then
    v=$1
    shift 1
else
    v=
fi

function df_() {
    free=$(df -B1 "$1")
    free=${free% *%*}
    free=${free%%*( )}
    free=${free##* }
    echo "${free}"
}

function mv_() {
    src=$1

    if [[ ! -e /var/jb/var/stash ]]; then
        mkdir -p /var/jb/var/db/stash
        /var/jb/usr/libexec/cydia/setnsfpn /var/jb/var/db/stash
        ln -s -t /var/jb/var /var/jb/var/db/stash
    elif [[ -d /var/jb/var/stash ]]; then
        /var/jb/usr/libexec/cydia/setnsfpn /var/jb/var/stash
    fi

    tmp=$(mktemp -d /var/jb/var/stash/_.XXXXXX)
    dst=${tmp}/${src##*/}

    chmod 755 "${tmp}"
    chown root.admin "${tmp}"

    mkdir -- "${dst}" || {
        rmdir -- "${tmp}"
        exit 1
    }

    echo -n "${src}" >"${tmp}.lnk"

    if [[ -e ${src} ]]; then
        chmod --reference="${src}" "${dst}"
        chown --reference="${src}" "${dst}"

        cp -aT $v "${src}" "${dst}" || {
            rm -rf "${tmp}"
            exit 1
        }

        mv $v "${src}" "${src}.moved"
        ln -s "${dst}" "${src}"
        rm -rf $v "${src}.moved"
    else
        chmod 775 "${dst}"
        chown root.admin "${dst}"
        ln -s "${dst}" "${src}"
    fi
}

function shift_() {
    dir=${1%/}

    if [[ -d ${dir} && ! -h ${dir} ]]; then
        used=$(/var/jb/usr/libexec/cydia/du -bs "${dir}")
        used=${used%%$'\t'*}
        free=$(df_ /var/jb/var)

        if [[ $((used + 524288)) -lt ${free} ]]; then
            mv_ "${dir}"
        fi
    elif [[ ! -e ${dir} ]]; then
        rm -f "${dir}"
        mv_ "${dir}"
    fi
}

shift_ "$@"
