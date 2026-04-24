```
source env.sh
cd build
make clean && make demo
/
make clean && make yolo
```

`rv1106_ipc_custom` or `yolo_test` will be produce under `build/`

move `rv1106_ipc_custom` or `yolo_test` to `/userdata/
move `run_init.sh` to /userdata/ and rename to `rv1106_ipc`

if test only yolo, run `chmod +x /userdata/yolo_test && ./yolo_test`

if replace rv1106_ipc, run 
```
chmod +x /userdata/rv1106_ipc && reboot
```