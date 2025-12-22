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


2) create a service to create the pipe:
   a) if systemd create this file: _/etc/systemd/system/waydroid-gpsd-stream.service_
```
[Unit]
Description=Streams GPSD JSON data to a FIFO for Waydroid
After=gpsd.service
Requires=gpsd.service

[Service]
Type=simple
Environment=FIFO_PATH=/tmp/gpsd_pipe

ExecStartPre=/bin/sh -c '\
    if [ ! -p "$FIFO_PATH" ]; then \
        rm -f "$FIFO_PATH"; \
        mkfifo "$FIFO_PATH"; \
        chmod 666 "$FIFO_PATH"; \
    fi'

# gpspipe sends WATCH itself — no socat, no protocol abuse
ExecStart=/bin/sh -c 'exec gpspipe --json > "$FIFO_PATH"'

Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```
   b) run:
```
sudo systemctl daemon-reload
sudo systemctl enable --now waydroid-gpsd-stream.service
sudo reboot
```

   a) if openRC create this file: _/raspberrypi/etc/init.d/waydroid-gps_
```
#!/sbin/openrc-run

# Description of the service
description="Streams GPSD JSON data to a FIFO for Waydroid"

depend() {
    need gpsd
    after gpsd
}

# Configuration
FIFO_PATH="/tmp/gpsd_pipe"
PIDFILE="/run/${RC_SVCNAME}.pid"

start() {
    ebegin "Starting Waydroid GPS Streamer"
    
    # Ensure the FIFO exists with correct permissions
    if [ ! -p "$FIFO_PATH" ]; then
        mkfifo "$FIFO_PATH"
        chmod 666 "$FIFO_PATH"
    fi

    # Start the stream in the background
    # 1. Send the WATCH command to gpsd to trigger JSON output
    # 2. Use gpspipe to capture the raw JSON stream and pipe to FIFO
    # (echo '?WATCH={"enable":true,"json":true};' | socat - TCP:127.0.0.1:2947; gpspipe --json) > /tmp/gps_pipe &

    start-stop-daemon --start --background \
        --make-pidfile --pidfile "$PIDFILE" \
        --exec /bin/sh -- -c \
        "echo '?WATCH={\"enable\":true,\"json\":true};' | socat - TCP:127.0.0.1:2947; exec gpspipe --json > $FIFO_PATH"
    
    eend $?
}

stop() {
    ebegin "Stopping Waydroid GPS Streamer"
    start-stop-daemon --stop --pidfile "$PIDFILE"
    rm -f "$PIDFILE"
    eend $?
}
```
   b) run:
```
sudo rc-update add waydroid-gps
sudo reboot
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

Make sure gpspipe and socat commands are installed
