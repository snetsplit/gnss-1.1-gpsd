class IGnssLocationListener {
public:
    virtual ~IGnssLocationListener() = default;
    virtual void onLocationUpdated(const GnssLocation& location) = 0;
};
