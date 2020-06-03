#!/bin/sh

set -ex

apk update
apk add g++ make python3

cd /root
wget https://raw.githubusercontent.com/account-login/make.py/master/make.py -O /usr/local/bin/make.py
chmod +x /usr/local/bin/make.py

cd /root/proj/
export LD_FLAGS=-static
make.py -v -j16 all
