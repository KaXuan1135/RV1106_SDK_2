all: demo

date=`date +%m%d`

yolo: 
	cd ../ai && make clean && make
	cd ../yolo && make clean && make

demo:ai
	cd ../app  && make
	cd ../demo  && make clean && make
ai:
	cd ../ai && make
clean: 
	cd ../demo && make clean
	cd ../app && make clean
	cd ../ai && make clean
	rm -rf rv1106_ipc_custom
	rm -rf yolo_test
install:
	\cp -rf rv1106_ipc /home/nosword/nfs/rv1106
	\cp -rf rv1106_ipc /mnt/hgfs/work/tftp
