This project adds a way to get GPS on Waydroid from a host's GPSD server. It exists because I had a lot of trouble getting GPS to work reliably on Waydroid using existing techniques. It works good for my use case. Hopefully if yours is different it'll be a good starting point to make your own.

Currently this impliments basic location features (lat, long, alttitude, accuracy, track/bearing ). More advanced things like power management are device dependant and outside the scope. Other features like Geo Fencing functions might be implimented if a need is demonstrated.

If you have any suggestions or issues please open a ticket with logs if relevent. That said, please don't DOX yourself. If your logs have gps coordinets, please replace the lat,long values with \[redactacted\]. See [https://www.rfwireless-world.com/terminology/gps-nmea-sentences](gps-nmea-sentences) for what to look for redacting the location from NMEA sentences, if your have NMEA logs (you shouldn't though, it's not something this interacts with). I don't really need to know where you are to help you know where you are.


Install:
This needs to be compiled into a Waydroid image. To do this, see: [Build.md](Build.md)


The host needs to be configured to share with Waydroid. To do this, see [EndUser.md](EndUser.md)
