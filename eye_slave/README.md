# Slave eye (viewer's right)

Same firmware as the master in the folder above. Only two settings differ, set in this folder's
`CMakeLists.txt`: `APP_EYE_IS_LEFT=0` and `APP_SYNC_ROLE=2` (ESP-NOW slave).

It holds the eye centered and never blinks on its own. It follows the master's gaze and copies its
blinks over ESP-NOW; if the master goes quiet for 1 s it eases back to center.

```bash
. ~/esp/esp-idf-5.5.5/export.sh
idf.py -B ~/esp/build-eye-slave build
```

Flash from the build folder to the slave's COM port (each board gets its own port, both can be plugged in):

```bash
py -m esptool --chip esp32s3 -p COM7 -b 921600 write-flash "@flash_args"
```
