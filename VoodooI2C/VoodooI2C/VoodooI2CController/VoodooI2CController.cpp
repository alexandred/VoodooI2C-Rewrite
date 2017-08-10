//
//  VoodooI2CController.cpp
//
//  Created by Alexandre on 31/07/2017.
//  Copyright © 2017 Alexandre Daoud. All rights reserved.
//

#include "VoodooI2CController.hpp"
#include "VoodooI2CControllerNub.hpp"

#define super IOService
OSDefineMetaClassAndStructors(VoodooI2CController, IOService);

/**
 Frees VoodooI2CController class - releases objects instantiated in `init`
 */

void VoodooI2CController::free() {
    IOFree(physical_device, sizeof(VoodooI2CControllerPhysicalDevice));

    super::free();
}

/**
 Initialises VoodooI2CController class

 @param properties OSDictionary* representing the matched personality

 @return returns true on successful initialisation, else returns false
 */

bool VoodooI2CController::init(OSDictionary* properties) {
    if (!super::init(properties)) {
        if (debug_logging)
            IOLog("%s super::init failed\n", getName());
        return false;
    }

    physical_device = reinterpret_cast<VoodooI2CControllerPhysicalDevice*>(IOMalloc(sizeof(VoodooI2CControllerPhysicalDevice)));
    memset(physical_device, 0, sizeof(VoodooI2CControllerPhysicalDevice));
    physical_device->awake = true;

    return true;
}

/**
 Maps the controller's memory to a virtual address
 */

IOReturn VoodooI2CController::mapMemory() {
    if (physical_device->provider->getDeviceMemoryCount() == 0) {
        return kIOReturnDeviceError;
    } else {
        physical_device->mmap = physical_device->provider->mapDeviceMemoryWithIndex(0);
        if (!physical_device->mmap) return kIOReturnDeviceError;
        return kIOReturnSuccess;
    }
}

/**
 Implemented to beat Apple's own LPSS kexts
 
 @param provider IOService* representing the matched entry in the IORegistry
 @param score    Probe score as specified in the matched personality
 
 @return returns a pointer to this instance of VoodooI2CController
 */

VoodooI2CController* VoodooI2CController::probe(IOService* provider, SInt32* score) {
    if (!super::probe(provider, score)) {
        if (debug_logging)
            IOLog("%s::%s super::probe failed", getName(), getMatchedName(provider));
        return NULL;
    }

    return this;
}

/**
 Publishes a `VoodooI2CControllerNub` entry into the IORegistry for matching

 @return returns kIOReturnSuccess if successful, else kIOReturnError
 */

IOReturn VoodooI2CController::publishNub() {
    IOLog("%s::%s Publishing nub\n", getName(), physical_device->name);

    nub = new VoodooI2CControllerNub;

    if (!nub || !nub->init()) {
        IOLog("%s::%s Could not initialise nub", getName(), physical_device->name);
        goto exit;
    }

    if (!nub->attach(this)) {
        IOLog("%s::%s Could not attach nub", getName(), physical_device->name);
        goto exit;
    }

    if (!nub->start(this)) {
        IOLog("%s::%s Could not start nub", getName(), physical_device->name);
        goto exit;
    }

    nub->registerService();

    return kIOReturnSuccess;

exit:
    if (nub) {
        nub->stop(this);
            nub->release();
            nub = NULL;
    }

    return kIOReturnError;
}

/**
 Reads the register of the controller at the offset

 @param offset

 @return returns the value of the register
 */
UInt32 VoodooI2CController::readRegister(int offset) {
    return *(const volatile UInt32 *)(physical_device->mmap->getVirtualAddress() + offset);
}

/**
 Releases the driver resources retained by `start`
 */

void VoodooI2CController::releaseResources() {
    if (physical_device) {
        if (nub) {
            nub->detach(this);
            OSSafeRelease(nub);
        }

        if (physical_device->mmap) {
            physical_device->mmap->release();
            physical_device->mmap = NULL;
        }

        physical_device->provider->close(this);
        OSSafeReleaseNULL(physical_device->provider);
    }

    PMstop();
}

/**
 Called by the system's power manager to set power states

 @param whichState either kIOPMPowerOff or kIOPMPowerOn
 @param whatDevice Power management policy maker

 @return returns kIOPMAckImplied if power state has been set else maximum number of milliseconds needed for the device to be in the correct state
 */

IOReturn VoodooI2CController::setPowerState(unsigned long whichState, IOService* whatDevice) {
    return kIOPMAckImplied;
}

/**
 Starts the physical I2C controller
 
 @param provider IOService* representing the matched entry in the IORegistry
 
 @return returns true on succesful start, else returns false
 */

bool VoodooI2CController::start(IOService* provider) {
    if (!super::start(provider)) {
        goto exit;
    }

    physical_device->provider = provider;
    physical_device->name = getMatchedName(physical_device->provider);

    PMinit();
    physical_device->provider->joinPMtree(this);
    registerPowerDriver(this, VoodooI2CIOPMPowerStates, kVoodooI2CIOPMNumberPowerStates);

    IOLog("%s::%s Starting I2C controller\n", getName(), physical_device->name);

    physical_device->provider->retain();
    if (!physical_device->provider->open(this)) {
        IOLog("%s::%s Could not open provider\n", getName(), physical_device->name);
    }

    return true;

exit:
    releaseResources();
    return false;
}

/**
 Stops the physical I2C controller and undoes the effects of `start` and `probe`

 @param provider IOService* representing the matched entry in the IORegistry
 */

void VoodooI2CController::stop(IOService* provider) {
    if (nub) {
        nub->stop(this);
        nub->release();
        nub = NULL;
    }

    releaseResources();

    super::stop(provider);
}

/**
 Writes the `value` into the controller's register at the `offset`

 @param value
 @param offset
 */

void VoodooI2CController::writeRegister(UInt32 value, int offset) {
    *(volatile UInt32 *)(physical_device->mmap->getVirtualAddress() + offset) = value;
}
