#!/bin/bash
BBCIM=/home/jules/stuff/chunkydemo2/bbcim/bbcim
./tileconv candy3.gif -o tiles.s
ca65 tiles.s -o tiles.o
6502-gcc -finline-limit=8 -mmach=bbcmaster -mcpu=65C02 -Os render.c tiles.s -Wl,-D,__STACKTOP__=0x42ff -o render -save-temps -Wl,-m,render.map

rm -rf tmpdisk
mkdir tmpdisk
cp render render.inf "!boot" "!boot.inf" tmpdisk

BINSIZE=$(wc -c render | awk '{print $1}')
echo "binary size: $BINSIZE / $(( 0x4300 - 0xe00 ))"

$BBCIM -new rendertest.ssd
pushd tmpdisk
$BBCIM -a ../rendertest.ssd *
popd

