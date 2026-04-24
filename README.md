source env.sh
cd build
make clean && make demo
/
make clean && make yolo

under build, there will be `rv1106_ipc`, move it to rv1106 `/userdata/rv1106_ipc`

killall rerun.sh ; killall -9 rv1106_ipc
chmod +x rv1106_ipc && ./rv1106_ipc

then restart (the device will auto run the /userdata/rv1106_ipc or you killed the previous one and run yourself)



板子启动时，默认会运行/oem/usr/bin/rv1106_ipc 程序， 如果不想其运行，可以kill掉 （命令行： killall rerun.sh ; killall -9 rv1106_ipc）; 
客制化：如果板上有 /userdata/run_init.sh（注意必须有 x 属性） 文件， 启动时会优先运行它， 不会再跑 默认的rv1106_ipc.
        如果板上没有 /userdata/run_init.sh， 而有/userdata/rv1106_ipc ， 启动时会优先运行它，不会再跑 默认的rv1106_ipc.

outdated, the system never run `/userdata/run_init.sh`, it only run `/userdata/rv1106_ipc` 

troiley method:
rename `/userdata/rv1106_ipc` to `/userdata/rv1106_ipc_custom`
rename `/userdata/run_init.sh` to `/userdata/rv1106_ipc`, and run `/userdata/rv1106_ipc_custom` in the `/userdata/rv1106_ipc`

