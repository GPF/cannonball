sh-elf-objcopy -R .stack -O binary cannonball.elf cannonball.bin
$KOS_BASE/utils/scramble/scramble cannonball.bin 1st_read.bin
# cp 1st_read.bin cd/
#mkisofs -l -J -C 0,11702 -G $KOS_BASE/../IP.BIN -V "DREAMCAST" -o dc.iso cd
#$KOS_BASE/utils/cdi4dc/cdi4dc dc.iso oldvice.cdi -d
# Define the command to find and format the directories
#DIRS=$(find . -type d | grep 'cd/' | sed 's/^/-d /' | tr '\n' ' ')
#echo $DIRS
mkdcdisc  -n DCCannonball -d cd/ -e cannonball.elf -o cannonball.cdi  -N 


