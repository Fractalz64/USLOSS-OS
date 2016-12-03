#!/bin/ksh

if [ "$#" -eq 0 ] 
then
    echo "Usage: ksh testphase5.ksh <num>"
    echo "where <num> is 1, 2, ... or 8"
    exit 1
fi

num=$1
if [ -f simple${num} ]
then
    /bin/rm simple${num}
fi

# Copy disk and terminal files
cp testcases/disk0.orig disk0
cp testcases/disk1.orig disk1

if  make simple${num} 
then
    ./simple${num} 
fi
echo
