This project adds a way to get GPS on Waydroid from a host's GPSD server. It exists because I had a lot of trouble getting GPS to work reliably on Waydroid.


Install:
This needs to be compiled into a Waydroid image. To do this:

1) set up a Waydroid Lineage build enviroment, see: https://docs.waydro.id/development/compile-waydroid-lineage-os-based-images

2) in the build enviroment create this file: *.repo/local_manifests/04-gps.xml*:
```
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
    <remote
        name="gnssgpsd"
        fetch="https://github.com/snetsplit" />

    <project
        name="gnss-1.1-gpsd"
        path="hardware/waydroid/gnss/1.1-gpsd"
        revision="lineage20.0"
        remote="gnssgpsd" />
</manifest>
```

3) in the root of the build enviroment run: 
```
repo sync hardware/waydroid/gnss/1.1-gpsd --force-sync
```

4) in device/waydroid/waydroid/device.mk add these lines:
```

PRODUCT_PACKAGES += android.hardware.gnss@1.1 \
        android.hardware.gnss@1.1-impl \
        android.hardware.gnss@1.1-service-gpsd

```  
5) build Lineage as usual






The host needs to be configured:

1) once using your newly built images you need to pipe GPSD into Waydroid. On the host edit */var/lib/waydroid/lxc/waydroid/config_nodes* and at the bottom (make sure no duplicate entries dev/ttyGPSD):

```
lxc.mount.entry = /tmp/gpsd_pipe dev/ttyGPSD none bind,optional,create=file 0 0
```

2) create a shell script and configure it to run prior to Waydroid:
```
#use a fifo to pipe GPSD output, insulting the LXC from GPSD restarts
if [ ! -p "/tmp/gpsd_pipe" ]; then
    mkfifo "/tmp/gpsd_pipe"
    chmod 666 "/tmp/gpsd_pipe"
fi
(echo '?WATCH={"enable":true,"json":true};' | socat - TCP:127.0.0.1:2947; gpspipe --json) > /tmp/gps_pipe &

#optional if waydroid currently isn't running
waydroid container restart
```

**Troubleshooting:**
```
sudo waydroid shell cat /dev/ttyGPSD
```

 Should have JSON output similar to:
```
{"class":"SKY","device":"/dev/pts/4","hdop":0.83,"pdop":1.37,"vdop":1.08,"uSat":12}
{"class":"DEVICE","path":"/dev/pts/4","activated":"2025-12-22T02:34:36.721Z"}
```
