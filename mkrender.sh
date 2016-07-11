#!/bin/bash
BBCIM=/home/jules/stuff/chunkydemo2/bbcim/bbcim
./tileconv candy3.gif -o tiles.s
ca65 tiles.s -o tiles.o
6502-gcc -mmach=bbcmaster -mcpu=65C02 -Os render.c tiles.s -Wl,-D,__STACKTOP__=0x42ff -o render -save-temps -Wl,-m,render.map

rm -rf tmpdisk
mkdir tmpdisk
cp render render.inf "!boot" "!boot.inf" tmpdisk

$BBCIM -new rendertest.ssd
pushd tmpdisk
$BBCIM -a ../rendertest.ssd *
popd

