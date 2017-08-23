//
//  VoodooI2CHIDDeviceWrapper.cpp
//  VoodooI2CHID
//
//  Created by CoolStar on 8/22/17.
//  Copyright © 2017 CoolStar. All rights reserved.
//

#include "VoodooI2CHIDDeviceWrapper.hpp"
#include "VoodooI2CHIDDevice.hpp"

OSDefineMetaClassAndStructors(VoodooI2CHIDDeviceWrapper, IOHIDDevice)

bool VoodooI2CHIDDeviceWrapper::start(IOService *provider) {
    if (OSDynamicCast(VoodooI2CHIDDevice, provider) == NULL)
        return false;
    
    this->provider = OSDynamicCast(VoodooI2CHIDDevice, provider);
    
    setProperty("HIDDefaultBehavior", OSString::withCString("Mouse"));
    return IOHIDDevice::start(provider);
}

IOReturn VoodooI2CHIDDeviceWrapper::newReportDescriptor(IOMemoryDescriptor **descriptor) const {
    IOLog("%s::Requested HID Descriptor\n", getName());
    if (this->provider->ReportDescLength == 0)
        return kIOReturnDeviceError;
    
    IOBufferMemoryDescriptor *buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, this->provider->ReportDescLength);
    if (!buffer)
        return kIOReturnNoResources;
    buffer->writeBytes(0, this->provider->ReportDesc, this->provider->ReportDescLength);
    IOLog("%s::Write %d bytes into buffer\n", getName(), this->provider->ReportDescLength);
    *descriptor = buffer;
    return kIOReturnSuccess;
}

OSNumber* VoodooI2CHIDDeviceWrapper::newVendorIDNumber() const {
    return OSNumber::withNumber(this->provider->HIDDescriptor.wVendorID, 16);
}

OSNumber* VoodooI2CHIDDeviceWrapper::newProductIDNumber() const {
    return OSNumber::withNumber(this->provider->HIDDescriptor.wProductID, 16);
}

OSNumber* VoodooI2CHIDDeviceWrapper::newVersionNumber() const {
    return OSNumber::withNumber(this->provider->HIDDescriptor.wVersionID, 16);
}

OSString* VoodooI2CHIDDeviceWrapper::newTransportString() const {
    return OSString::withCString("I2C");
}

OSString* VoodooI2CHIDDeviceWrapper::newManufacturerString() const {
    return OSString::withCString("Apple");
}

OSNumber* VoodooI2CHIDDeviceWrapper::newPrimaryUsageNumber() const {
    return OSNumber::withNumber(kHIDUsage_GD_Mouse, 32);
}

OSNumber* VoodooI2CHIDDeviceWrapper::newPrimaryUsagePageNumber() const {
    return OSNumber::withNumber(kHIDPage_GenericDesktop, 32);
}
