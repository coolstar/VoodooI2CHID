//
//  VoodooI2CHIDDeviceWrapper.hpp
//  VoodooI2CHID
//
//  Created by CoolStar on 8/22/17.
//  Copyright © 2017 CoolStar. All rights reserved.
//

#ifndef VoodooI2CHIDDeviceWrapper_hpp
#define VoodooI2CHIDDeviceWrapper_hpp
#include <IOKit/hid/IOHIDDevice.h>

class VoodooI2CHIDDevice;
class VoodooI2CHIDDeviceWrapper : public IOHIDDevice {
    OSDeclareDefaultStructors(VoodooI2CHIDDeviceWrapper)
public:
    VoodooI2CHIDDevice *provider;
    
    virtual bool start(IOService *provider) override;
    
    virtual IOReturn newReportDescriptor(IOMemoryDescriptor **descriptor) const override;
    virtual IOReturn setReport(IOMemoryDescriptor *report, IOHIDReportType reportType, IOOptionBits options) override;
    virtual OSNumber* newVendorIDNumber() const override;
    virtual OSNumber* newProductIDNumber() const override;
    virtual OSNumber* newVersionNumber() const override;
    virtual OSString* newTransportString() const override;
    virtual OSString* newManufacturerString() const override;
    virtual OSNumber* newPrimaryUsageNumber() const override;
    virtual OSNumber* newPrimaryUsagePageNumber() const override;
};

#endif /* VoodooI2CHIDDeviceWrapper_hpp */
