NUM=$1

F_DL=~/mnt/xfs/tmp_download$NUM
F_STUB=~/mnt/xfs/stub$NUM

rm -rf $F_DL $F_STUB

echo DOWNLOAD_DATA > $F_DL
echo GARBAGE > $F_STUB

setfattr -n user.movedata.download1 -v 123 $F_DL
setfattr -n user.movedata.download2 -v 456 $F_DL
setfattr -n user.movedata.download3 -v 789 $F_DL
setfattr -n user.movedata.stub1 -v abc $F_STUB
setfattr -n user.movedata.stub2 -v xyz $F_STUB
setfattr -n user.movedata.stub3 -v vuw $F_STUB

getfattr -d $F_DL
getfattr -d $F_STUB

# stat $F_DL $F_STUB
sudo ./xfs_move_data $F_DL $F_STUB 
# stat $F_DL $F_STUB

getfattr -d $F_DL
getfattr -d $F_STUB
