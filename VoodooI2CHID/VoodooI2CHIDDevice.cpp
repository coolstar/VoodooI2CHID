//
//  VoodooI2CHIDDevice.cpp
//  VoodooI2CHID
//
//  Created by CoolStar on 8/21/17.
//  Copyright © 2017 CoolStar. All rights reserved.
//

#include "VoodooI2CHIDDevice.hpp"
#include "VoodooI2CHIDDeviceWrapper.hpp"
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
    
    if (fetchHIDDescriptor() != kIOReturnSuccess){
        IOLog("%s::Unable to get HID Descriptor!\n", getName());
        return false;
    }
    
    this->ReportDescLength = 0;
    if (fetchReportDescriptor() != kIOReturnSuccess){
        IOLog("%s::Unable to get Report Descriptor!\n", getName());
        stop(provider);
        return false;
    }
    
    this->workLoop = getWorkLoop();
    if (!this->workLoop){
        IOLog("%s::Unable to get workloop\n", getName());
        stop(provider);
        return false;
    }
    
    this->workLoop->retain();
    
    this->wrapper = new VoodooI2CHIDDeviceWrapper;
    if (this->wrapper->init()){
        this->wrapper->attach(this);
        this->wrapper->start(this);
    } else {
        this->wrapper->release();
        this->wrapper = NULL;
    }
    
    registerService();
    return true;
}

void VoodooI2CHIDDevice::stop(IOService *provider){
    IOLog("%s::Stopping!\n", getName());
    
    if (this->ReportDescLength != 0){
        IOFree(this->ReportDesc, this->ReportDescLength);
    }
    
    if (this->wrapper){
        this->wrapper->terminate(kIOServiceRequired | kIOServiceSynchronous);
        this->wrapper->release();
        this->wrapper = NULL;
    }
    
    OSSafeReleaseNULL(this->workLoop);
    
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
    UInt16 flags = I2C_M_RD;
    if (this->use10BitAddressing)
        flags = I2C_M_RD | I2C_M_TEN;
    VoodooI2CControllerBusMessage msgs[] = {
        {
            .address = this->i2cAddress,
            .buffer = values,
            .flags = flags,
            .length = (UInt16)len,
        },
    };
    return i2cController->transferI2C(msgs, 1);
}

IOReturn VoodooI2CHIDDevice::writeI2C(UInt8 *values, UInt16 len){
    UInt16 flags = 0;
    if (this->use10BitAddressing)
        flags = I2C_M_TEN;
    VoodooI2CControllerBusMessage msgs[] = {
        {
            .address = this->i2cAddress,
            .buffer = values,
            .flags = flags,
            .length = (UInt16)len,
        },
    };
    return i2cController->transferI2C(msgs, 1);
}

IOReturn VoodooI2CHIDDevice::writeReadI2C(UInt8 *writeBuf, UInt16 writeLen, UInt8 *readBuf, UInt16 readLen){
    UInt16 readFlags = I2C_M_RD;
    if (this->use10BitAddressing)
        readFlags = I2C_M_RD | I2C_M_TEN;
    UInt16 writeFlags = 0;
    if (this->use10BitAddressing)
        writeFlags = I2C_M_TEN;
    VoodooI2CControllerBusMessage msgs[] = {
        {
            .address = this->i2cAddress,
            .buffer = writeBuf,
            .flags = writeFlags,
            .length = writeLen,
        },
        {
            .address = this->i2cAddress,
            .buffer = readBuf,
            .flags = readFlags,
            .length = readLen,
        }
    };
    return i2cController->transferI2C(msgs, 2);
}

IOReturn VoodooI2CHIDDevice::fetchHIDDescriptor(){
    UInt8 length = 2;
    
    union command cmd;
    cmd.c.reg = this->HIDDescriptorAddress;
    
    memset((UInt8 *)&this->HIDDescriptor, 0, sizeof(i2c_hid_descr));
    
    if (writeReadI2C(cmd.data, (UInt16)length, (UInt8 *)&this->HIDDescriptor, (UInt16)sizeof(i2c_hid_descr)) != kIOReturnSuccess)
        return kIOReturnIOError;
    
    IOLog("%s::BCD Version: 0x%x\n", getName(), this->HIDDescriptor.bcdVersion);
    if (this->HIDDescriptor.bcdVersion == 0x0100 && this->HIDDescriptor.wHIDDescLength == sizeof(i2c_hid_descr)){
        setProperty("HIDDescLength", (UInt32)this->HIDDescriptor.wHIDDescLength, 32);
        setProperty("bcdVersion", (UInt32)this->HIDDescriptor.bcdVersion, 32);
        setProperty("ReportDescLength", (UInt32)this->HIDDescriptor.wReportDescLength, 32);
        setProperty("ReportDescRegister", (UInt32)this->HIDDescriptor.wReportDescRegister, 32);
        setProperty("InputRegister", (UInt32)this->HIDDescriptor.wInputRegister, 32);
        setProperty("MaxInputLength", (UInt32)this->HIDDescriptor.wMaxInputLength, 32);
        setProperty("OutputRegister", (UInt32)this->HIDDescriptor.wOutputRegister, 32);
        setProperty("MaxOutputLength", (UInt32)this->HIDDescriptor.wMaxOutputLength, 32);
        setProperty("CommandRegister", (UInt32)this->HIDDescriptor.wCommandRegister, 32);
        setProperty("DataRegister", (UInt32)this->HIDDescriptor.wDataRegister, 32);
        setProperty("vendorID", (UInt32)this->HIDDescriptor.wVendorID, 32);
        setProperty("productID", (UInt32)this->HIDDescriptor.wProductID, 32);
        setProperty("VersionID", (UInt32)this->HIDDescriptor.wVersionID, 32);
        return kIOReturnSuccess;
    }
    return kIOReturnDeviceError;
}

IOReturn VoodooI2CHIDDevice::fetchReportDescriptor(){
    if (this->ReportDescLength != 0)
        return kIOReturnSuccess;
    this->ReportDescLength = this->HIDDescriptor.wReportDescLength;
    uint8_t length = 2;
    
    union command cmd;
    cmd.c.reg = this->HIDDescriptor.wReportDescRegister;
    
    this->ReportDesc = (UInt8 *)IOMalloc(this->ReportDescLength);
    memset((UInt8 *)this->ReportDesc, 0, this->ReportDescLength);
    
    if (writeReadI2C(cmd.data, (UInt16)length, (UInt8 *)this->ReportDesc, this->ReportDescLength) != kIOReturnSuccess)
        return kIOReturnIOError;
    return kIOReturnSuccess;
}
