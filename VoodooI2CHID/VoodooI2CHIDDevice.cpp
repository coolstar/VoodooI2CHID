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
    
    this->DeviceIsAwake = false;
    this->IsReading = true;
    
    PMinit();
    
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
        PMstop();
        return false;
    }
    
    IOLog("%s::Got HID Descriptor Address!\n", getName());
    
    if (fetchHIDDescriptor() != kIOReturnSuccess){
        IOLog("%s::Unable to get HID Descriptor!\n", getName());
        PMstop();
        return false;
    }
    
    this->ReportDescLength = 0;
    if (fetchReportDescriptor() != kIOReturnSuccess){
        IOLog("%s::Unable to get Report Descriptor!\n", getName());
        stop(provider);
        return false;
    }
    
    this->IsReading = false;
    
    this->workLoop = getWorkLoop();
    if (!this->workLoop){
        IOLog("%s::Unable to get workloop\n", getName());
        stop(provider);
        return false;
    }
    
    this->workLoop->retain();
    
    this->interruptSource = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &VoodooI2CHIDDevice::InterruptOccured), provider, 0);
    if (!this->interruptSource) {
        IOLog("%s::Unable to get interrupt source\n", getName());
        stop(provider);
        return false;
    }
    
    this->workLoop->addEventSource(this->interruptSource);
    this->interruptSource->enable();
    
    this->wrapper = new VoodooI2CHIDDeviceWrapper;
    if (this->wrapper->init()){
        this->wrapper->attach(this);
        this->wrapper->start(this);
    } else {
        this->wrapper->release();
        this->wrapper = NULL;
    }
    
    registerService();
    
    reset_dev();
    
    this->DeviceIsAwake = true;
    this->IsReading = false;
    
#define kMyNumberOfStates 2
    
    static IOPMPowerState myPowerStates[kMyNumberOfStates];
    // Zero-fill the structures.
    bzero (myPowerStates, sizeof(myPowerStates));
    // Fill in the information about your device's off state:
    myPowerStates[0].version = 1;
    myPowerStates[0].capabilityFlags = kIOPMPowerOff;
    myPowerStates[0].outputPowerCharacter = kIOPMPowerOff;
    myPowerStates[0].inputPowerRequirement = kIOPMPowerOff;
    // Fill in the information about your device's on state:
    myPowerStates[1].version = 1;
    myPowerStates[1].capabilityFlags = kIOPMPowerOn;
    myPowerStates[1].outputPowerCharacter = kIOPMPowerOn;
    myPowerStates[1].inputPowerRequirement = kIOPMPowerOn;
    
    provider->joinPMtree(this);
    
    registerPowerDriver(this, myPowerStates, kMyNumberOfStates);
    return true;
}

void VoodooI2CHIDDevice::stop(IOService *provider){
    IOLog("%s::Stopping!\n", getName());
    
    this->DeviceIsAwake = false;
    IOSleep(1);
    
    if (this->ReportDescLength != 0){
        IOFree(this->ReportDesc, this->ReportDescLength);
    }
    
    if (this->interruptSource){
        this->interruptSource->disable();
        this->workLoop->removeEventSource(this->interruptSource);
        
        this->interruptSource->release();
        this->interruptSource = NULL;
    }
    
    if (this->wrapper){
        this->wrapper->terminate(kIOServiceRequired | kIOServiceSynchronous);
        this->wrapper->release();
        this->wrapper = NULL;
    }
    
    OSSafeReleaseNULL(this->workLoop);
    
    PMstop();
    
    super::stop(provider);
}

IOReturn VoodooI2CHIDDevice::setPowerState(unsigned long powerState, IOService *whatDevice){
    if (whatDevice != this)
        return kIOReturnInvalid;
    if (powerState == 0){
        //Going to sleep
        if (this->DeviceIsAwake){
            this->DeviceIsAwake = false;
            while (this->IsReading){
                IOSleep(10);
            }
            this->IsReading = true;
            set_power(I2C_HID_PWR_SLEEP);
            this->IsReading = false;
            
            IOLog("%s::Going to Sleep!\n", getName());
        }
    } else {
        if (!this->DeviceIsAwake){
            this->IsReading = true;
            reset_dev();
            this->IsReading = false;
            
            this->DeviceIsAwake = true;
            IOLog("%s::Woke up from Sleep!\n", getName());
        } else {
            IOLog("%s::Device already awake! Not reinitializing.\n", getName());
        }
    }
    return kIOPMAckImplied;
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

IOReturn VoodooI2CHIDDevice::set_power(int power_state){
    uint8_t length = 4;
    
    union command cmd;
    cmd.c.reg = this->HIDDescriptor.wCommandRegister;
    cmd.c.opcode = 0x08;
    cmd.c.reportTypeID = power_state;
    
    return writeI2C(cmd.data, length);
}

IOReturn VoodooI2CHIDDevice::reset_dev(){
    set_power(I2C_HID_PWR_ON);
    
    IOSleep(1);
    
    uint8_t length = 4;
    
    union command cmd;
    cmd.c.reg = this->HIDDescriptor.wCommandRegister;
    cmd.c.opcode = 0x01;
    cmd.c.reportTypeID = 0;
    
    writeI2C(cmd.data, length);
    return kIOReturnSuccess;
}

void VoodooI2CHIDDevice::get_input(OSObject* owner, IOTimerEventSource* sender) {
    uint16_t maxLen = this->HIDDescriptor.wMaxInputLength;
    
    unsigned char* report = (unsigned char *)IOMalloc(maxLen);
    
    readI2C(report, maxLen);
    
    int return_size = report[0] | report[1] << 8;
    if (return_size == 0) {
        IOLog("%s::0 sized report!\n", getName());
        this->IsReading = false;
        return;
    }
    
    if (return_size > maxLen) {
        IOLog("%s: Incomplete report %d/%d\n", getName(), maxLen, return_size);
        this->IsReading = false;
        return;
    }
    
    
    IOBufferMemoryDescriptor *buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, return_size);
    buffer->writeBytes(0, report + 2, return_size - 2);
    
    IOReturn err = this->wrapper->handleReport(buffer, kIOHIDReportTypeInput);
    if (err != kIOReturnSuccess)
        IOLog("%s::Error handling report: 0x%.8x\n", getName(), err);
    
    buffer->release();
    
    IOFree(report, maxLen);
    this->IsReading = false;
}

static void i2c_hid_readReport(VoodooI2CHIDDevice *device){
    device->get_input(NULL, NULL);
}

void VoodooI2CHIDDevice::InterruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount){
    if (this->IsReading)
        return;
    if (!this->DeviceIsAwake)
        return;
    
    this->IsReading = true;
    
    thread_t newThread;
    kern_return_t kr = kernel_thread_start((thread_continue_t)i2c_hid_readReport, this, &newThread);
    if (kr != KERN_SUCCESS){
        this->IsReading = false;
        IOLog("%s::Thread error!\n", getName());
    } else {
        thread_deallocate(newThread);
    }
    
}
