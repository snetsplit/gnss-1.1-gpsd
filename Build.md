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

