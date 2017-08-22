//
//  VoodooI2CHIDDevice.cpp
//  VoodooI2CHID
//
//  Created by CoolStar on 8/21/17.
//  Copyright © 2017 CoolStar. All rights reserved.
//

#include "VoodooI2CHIDDevice.hpp"
#include <IOKit/IOLib.h>

#define super IOService

#define I2C_HID_PWR_ON  0x00
#define I2C_HID_PWR_SLEEP 0x01

union command {
    uint8_t data[4];
    struct __attribute__((__packed__)) cmd {
        uint16_t reg;
        uint8_t reportTypeID;
        uint8_t opcode;
    } c;
};

struct __attribute__((__packed__))  i2c_hid_cmd {
    unsigned int registerIndex;
    uint8_t opcode;
    unsigned int length;
    bool wait;
};

OSDefineMetaClassAndStructors(VoodooI2CHIDDevice, IOService);

bool VoodooI2CHIDDevice::start(IOService *provider){
    if (!super::start(provider))
        return false;
    IOLog("%s::Starting!\n", getName());
    
    i2cController = OSDynamicCast(VoodooI2CControllerDriver, provider->getProvider());
    if (!i2cController){
        IOLog("%s::Unable to get I2C Controller!\n", getName());
        return false;
    }
    
    OSNumber *i2cAddress = OSDynamicCast(OSNumber, provider->getProperty("i2cAddress"));
    this->i2cAddress = i2cAddress->unsigned16BitValue();
    
    OSNumber *addrWidth = OSDynamicCast(OSNumber, provider->getProperty("addrWidth"));
    this->use10BitAddressing = (addrWidth->unsigned8BitValue() == 10);
    
    IOACPIPlatformDevice *acpiDevice = OSDynamicCast(IOACPIPlatformDevice, provider->getProperty("acpi-device"));
    if (getDescriptorAddress(acpiDevice) != kIOReturnSuccess){
        IOLog("%s::Unable to get HID Descriptor address!\n", getName());
        return false;
    }
    
    IOLog("%s::Fetching descriptor!\n", getName());
    if (fetchHIDDescriptor() != kIOReturnSuccess){
        IOLog("%s::Unable to get HID Descriptor!\n", getName());
        return false;
    }
    
    registerService();
    return true;
}

void VoodooI2CHIDDevice::stop(IOService *provider){
    IOLog("%s::Stopping!\n", getName());
    super::stop(provider);
}

IOReturn VoodooI2CHIDDevice::getDescriptorAddress(IOACPIPlatformDevice *acpiDevice){
    if (!acpiDevice)
        return kIOReturnNoDevice;
    
    UInt32 guid_1 = 0x3CDFF6F7;
    UInt32 guid_2 = 0x45554267;
    UInt32 guid_3 = 0x0AB305AD;
    UInt32 guid_4 = 0xDE38893D;
    
    OSObject *result = NULL;
    OSObject *params[4];
    char buffer[16];
    
    memcpy(buffer, &guid_1, 4);
    memcpy(buffer + 4, &guid_2, 4);
    memcpy(buffer + 8, &guid_3, 4);
    memcpy(buffer + 12, &guid_4, 4);
    
    
    params[0] = OSData::withBytes(buffer, 16);
    params[1] = OSNumber::withNumber(0x1, 8);
    params[2] = OSNumber::withNumber(0x1, 8);
    params[3] = OSNumber::withNumber((unsigned long long)0x0, 8);
    
    acpiDevice->evaluateObject("_DSM", &result, params, 4);
    if (!result)
        acpiDevice->evaluateObject("XDSM", &result, params, 4);
    if (!result)
        return kIOReturnNotFound;
    
    OSNumber* number = OSDynamicCast(OSNumber, result);
    if (number){
        setProperty("HIDDescriptorAddress", number);
        this->HIDDescriptorAddress = number->unsigned16BitValue();
    }
    
    if (result)
        result->release();
    
    params[0]->release();
    params[1]->release();
    params[2]->release();
    params[3]->release();
    
    if (!number)
        return kIOReturnInvalid;
    
    return kIOReturnSuccess;
}

IOReturn VoodooI2CHIDDevice::readI2C(UInt8 *values, UInt16 len){
    IOLog("%s::readI2C!\n", getName());
    UInt16 flags = I2C_M_RD;
    if (this->use10BitAddressing)
        flags = I2C_M_RD | I2C_M_TEN;
    VoodooI2CControllerBusMessage msgs[] = {
        {
            .address = this->i2cAddress,
            .flags = flags,
            .length = (UInt16)len,
            .buffer = values,
        },
    };
    return i2cController->transferI2C(msgs, 1);
}

IOReturn VoodooI2CHIDDevice::writeI2C(UInt8 *values, UInt16 len){
    IOLog("%s::writeI2C!\n", getName());
    UInt16 flags = 0;
    if (this->use10BitAddressing)
        flags = I2C_M_TEN;
    VoodooI2CControllerBusMessage msgs[] = {
        {
            .address = this->i2cAddress,
            .flags = flags,
            .length = (UInt16)len,
            .buffer = values,
        },
    };
    return i2cController->transferI2C(msgs, 1);
}

IOReturn VoodooI2CHIDDevice::fetchHIDDescriptor(){
    UInt8 length = 2;
    
    union command cmd;
    cmd.c.reg = this->HIDDescriptorAddress;
    
    if (writeI2C(cmd.data, length) != kIOReturnSuccess)
        return kIOReturnIOError;
    
    memset(&this->HIDDescriptor, 0, sizeof(i2c_hid_descr));
    
    if (readI2C((UInt8 *)&this->HIDDescriptor, sizeof(i2c_hid_descr)) != kIOReturnSuccess)
        return kIOReturnIOError;
    
    IOLog("%s::BCD Version: 0x%x\n", getName(), this->HIDDescriptor.bcdVersion);
    if (this->HIDDescriptor.bcdVersion == 0x0100 && this->HIDDescriptor.wHIDDescLength == sizeof(i2c_hid_descr))
        return kIOReturnSuccess;
    return kIOReturnDeviceError;
}
