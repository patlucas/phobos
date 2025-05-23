#!/bin/sh

# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

# This scripts configure/compile/run phobos tests.

# (c) 2014-2022 CEA/DAM
# Licensed under the terms of the GNU Lesser GPL License version 2.1

set -xe

function check_c_api()
{
    cd rpms/RPMS/x86_64
    cat <<EOF > test_c_api.c
#include <phobos_store.h>
#include <phobos_admin.h>
EOF

    sudo dnf --cacheonly -y install phobos-[1-9]* phobos-devel*
    # Just compile, do not link. We just check the coherency between the headers
    # and the specfile
    gcc -c -o test_c_api.o test_c_api.c `pkg-config --cflags glib-2.0`
    rm test_c_api.*
    sudo dnf --cacheonly -y remove phobos phobos-devel
    cd -
}

#set phobos root as cwd from phobos/ci directory
cur_dir=$(dirname $(readlink -m $0))
cd "$cur_dir"/..

# export PKG_CONFIG_PATH=/usr/pgsql-9.4/lib/pkgconfig;
./autogen.sh

if [ "$1" != "check-valgrind" -a "$1" != "no-tests" ]; then
    ./configure $1
    make rpm
    check_c_api
    make clean || cat src/tests/test-suite.log
else
    ./configure $2
fi

make
# FIXME: when cloning the repo, some scripts do not have o+rx
# permissions, it is however necessary to execute them as postgres,
# as well as when running valgrind tests
chmod    o+rx . ..
chmod -R o+rx src tests

phobos_conf=tests/phobos.conf
test_db="$(grep "dbname" "$phobos_conf" | awk -F 'dbname=' '{print $2}' | \
           cut -d ' ' -f1)"

sudo -u postgres ./scripts/phobos_db_local drop_db -d "$test_db" || true
sudo -u postgres ./scripts/phobos_db_local setup_db -d "$test_db" -p phobos
export VERBOSE=1
if [ "$1" = "no-tests" ]; then
    make -j4
elif [ "$1" = "check-valgrind" ]; then
    sudo -E make check-valgrind
else
    sudo -E make check
fi
