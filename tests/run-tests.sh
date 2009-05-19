#!/bin/bash
#
# Copyright (c) 2009 Sun Microsystems, Inc., 4150 Network Circle, Santa
# Clara, California 95054, U.S.A. All rights reserved.
# 
# U.S. Government Rights - Commercial software. Government users are
# subject to the Sun Microsystems, Inc. standard license agreement and
# applicable provisions of the FAR and its supplements.
# 
# Use is subject to license terms.
# 
# This distribution may include materials developed by third parties.
# 
# Parts of the product may be derived from Berkeley BSD systems,
# licensed from the University of California. UNIX is a registered
# trademark in the U.S.  and in other countries, exclusively licensed
# through X/Open Company, Ltd.
# 
# Sun, Sun Microsystems, the Sun logo and Java are trademarks or
# registered trademarks of Sun Microsystems, Inc. in the U.S. and other
# countries.
# 
# This product is covered and controlled by U.S. Export Control laws and
# may be subject to the export or import laws in other
# countries. Nuclear, missile, chemical biological weapons or nuclear
# maritime end uses or end users, whether direct or indirect, are
# strictly prohibited. Export or reexport to countries subject to
# U.S. embargo or to entities identified on U.S. export exclusion lists,
# including, but not limited to, the denied persons and specially
# designated nationals lists is strictly prohibited.
# 
#

# Config variables
DOMAIN_CONFIG=domain_config
DOMAIN_NAME=GUK-Test
EXTRA=-XX:GUKMS=100 # allocate all initial memory to small pages
CONSOLE_OUT=/tmp/console.out
SUCCESSFUL=results_`date +%m.%d.%y`/successful
UNSUCCESSFUL=results_`date +%m.%d.%y`/unsuccessful
MAX_TEST_TIME=600
MAKE=gmake


# Execution variables
cd `dirname $0`
TEST_DIR=`pwd`
GUK_ROOT=$TEST_DIR/../

cd_guk() {
    cd $GUK_ROOT
}

cd_test_dir() {
    cd $TEST_DIR
}

destroy_domain() {
    # Destroy domain if it still exists
    if [ "`xm list | grep $DOMAIN_NAME`" ]; then
        xm destroy $DOMAIN_NAME
    fi    
}

domain_exists() {
    if [ "`xm list | grep $DOMAIN_NAME`" ]; then
        echo yes
    else
        echo no
    fi    
}

wait_on_domain() {
    # Let's sleep for couple of seconds to let xm create call finish it's
    # processing (otherwise we'll exit on failed domain_exists check immediately
    sleep 2
    COUNTER=2
    echo 
    while [ $COUNTER -lt $MAX_TEST_TIME ] && [ "`domain_exists`" == "yes" ]; do
        let COUNTER=COUNTER+1 
        sleep 1
    done
}

run_domain() {
    destroy_domain
    # Create the domain
    echo Creating the domain  
    # Dump the console output for at most $MAX_TEST_TIME
    echo `wait_on_domain` | xm create -f $DOMAIN_CONFIG name=$DOMAIN_NAME -c extra=$EXTRA > $CONSOLE_OUT
    echo Destroying the domain
    # Destroy the domain
    destroy_domain
}

prepare_sources() {
    cd_guk
    rm -f test.o test_rand.o guestvm-ukernel.o
    ln -s "${TEST_DIR}/test_rand.c" "test_rand.c"
    ln -s "${TEST_DIR}/${TEST_FILE}" "test.c"
}

compile_domain() {
    cd_guk
    $MAKE
    RET=$?
    rm -f test.c
    rm -f test_rand.c
}

TEST_FILES=`ls *_test.c 2> /dev/null`
if [ $# == 1 ]; then
    TEST_FILES=$1
fi


rm -rf ${TEST_DIR}/$SUCCESSFUL
rm -rf ${TEST_DIR}/$UNSUCCESSFUL
mkdir -p ${TEST_DIR}/$SUCCESSFUL
mkdir -p ${TEST_DIR}/$UNSUCCESSFUL
cd_test_dir
for TEST_FILE in $TEST_FILES; do
    echo TESTING using $TEST_FILE
    cd_guk
    prepare_sources
    compile_domain
    if [ $RET != 0 ]; then  
        continue
    fi
    run_domain
    # Check if the test was successful or not
    if [ "`cat $CONSOLE_OUT | grep SUCCESSFUL`" ]; then
       mv $CONSOLE_OUT ${TEST_DIR}/${SUCCESSFUL}/${TEST_FILE}.console 
    else
       mv $CONSOLE_OUT ${TEST_DIR}/${UNSUCCESSFUL}/${TEST_FILE}.console 
    fi
done

