//
//  VoodooI2CHIDDevice.hpp
//  VoodooI2CHID
//
//  Created by CoolStar on 8/21/17.
//  Copyright © 2017 CoolStar. All rights reserved.
//

#ifndef VoodooI2CHIDDevice_hpp
#define VoodooI2CHIDDevice_hpp

#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "VoodooI2CControllerDriver.hpp"

struct __attribute__((__packed__)) i2c_hid_descr {
    uint16_t wHIDDescLength;
    uint16_t bcdVersion;
    uint16_t wReportDescLength;
    uint16_t wReportDescRegister;
    uint16_t wInputRegister;
    uint16_t wMaxInputLength;
    uint16_t wOutputRegister;
    uint16_t wMaxOutputLength;
    uint16_t wCommandRegister;
    uint16_t wDataRegister;
    uint16_t wVendorID;
    uint16_t wProductID;
    uint16_t wVersionID;
    uint32_t reserved;
};

class VoodooI2CHIDDevice : public IOService
{
    OSDeclareDefaultStructors(VoodooI2CHIDDevice);
private:
    VoodooI2CControllerDriver *i2cController;
    IOService *provider;
    
    IOWorkLoop *workLoop;
    
    UInt16 i2cAddress;
    bool use10BitAddressing;
    UInt16 HIDDescriptorAddress;
    
    UInt8 *ReportDesc;
    UInt16 ReportDescLength;
    
    struct i2c_hid_descr HIDDescriptor;
    
    IOReturn getDescriptorAddress(IOACPIPlatformDevice *acpiDevice);
    
    IOReturn readI2C(UInt8 *values, UInt16 len);
    IOReturn writeI2C(UInt8 *values, UInt16 len);
    IOReturn writeReadI2C(UInt8 *writeBuf, UInt16 writeLen, UInt8 *readBuf, UInt16 readLen);
    
    IOReturn fetchHIDDescriptor();
    IOReturn fetchReportDescriptor();
    
public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
};

#endif /* VoodooI2CHIDDevice_hpp */
