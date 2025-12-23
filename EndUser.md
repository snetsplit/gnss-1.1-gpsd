
The host needs to be configured to pipe gpsd JSON into Waydroid:

1) Make sure gpsd, and gpspipe are installed on the linux host.

    a) On Gentoo: 
    ```
    sudo emerge --ask --noreplace sci-geosciences/gpsd
    ```
    b) On Debian based systems: 
    ```
    sudo apt install gpsd gpsd-clients
    ```

    
2) A service is needed to inject gspd output into Waydroid:

   a) if systemd based:
```
sudo wget -O /etc/systemd/system/waydroid-gpsd.service \
  https://raw.githubusercontent.com/snetsplit/gnss-1.1-gpsd/refs/heads/lineage20.0/waydroid-gpsd.service

sudo chmod +x /etc/systemd/system/waydroid-gpsd.service
sudo systemctl daemon-reload
sudo systemctl enable --now waydroid-gpsd.service
```

   b) if openRC based:
```
sudo wget -O /etc/init.d/waydroid-gpsd \
  https://raw.githubusercontent.com/snetsplit/gnss-1.1-gpsd/refs/heads/lineage20.0/waydroid-gpsd
  
sudo chmod +x /etc/init.d/waydroid-gpsd 
sudo rc-update add waydroid-gpsd
sudo rc-service waydroid-gpsd start
```


3) On the host edit */var/lib/waydroid/lxc/waydroid/config_nodes* and create the virtual mount. Be sure not to create a duplicate entry:

```
sudo sed -i '\|dev/ttyGPSDJSON|d' "/var/lib/waydroid/lxc/waydroid/config_nodes"
sudo echo "lxc.mount.entry = /tmp/gpsd_pipe dev/ttyGPSDJSON none bind,optional,create=file 0 0" >> /var/lib/waydroid/lxc/waydroid/config_nodes
```

verify only one entry
```
cat /var/lib/waydroid/lxc/waydroid/config_nodes | grep "ttyGPSDJSON"
```


**Troubleshooting:**
```
sudo waydroid shell cat /dev/ttyGPSDJSON
```

 Should have JSON output similar to:
```
{"class":"SKY","device":"/dev/pts/4","hdop":0.83,"pdop":1.37,"vdop":1.08,"uSat":12}
{"class":"DEVICE","path":"/dev/pts/4","activated":"2025-12-22T02:34:36.721Z"}
```



Run cgps to check gpsd output
