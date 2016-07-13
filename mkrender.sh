#!/bin/bash
set -e
rm -f rendertest.ssd
BBCIM=/home/jules/stuff/chunkydemo2/bbcim/bbcim
./tileconv candy3.gif -o tiles.s
ca65 tiles.s -o tiles.o
ld65 --config none.cfg -S 0xe00 tiles.o -o tiles

6502-gcc -mmach=bbcmaster -T rom.cfg -mcpu=65C02 -Os header.S render.c -Wl,-D,__STACKTOP__=0x40ff -o render -save-temps -Wl,-m,render.map

rm -rf tmpdisk
mkdir tmpdisk
cp render render.inf "!boot" "!boot.inf" "tiles" "tiles.inf" tmpdisk

BINSIZE=$(wc -c render | awk '{print $1}')
echo "binary size: $BINSIZE / 16384"

$BBCIM -new rendertest.ssd
pushd tmpdisk
$BBCIM -a ../rendertest.ssd *
popd

