//
//  VoodooI2CControllerDriver.cpp
//  VoodooI2C
//
//  Created by Alexandre on 03/08/2017.
//  Copyright © 2017 Alexandre Daoud. All rights reserved.
//

#include "VoodooI2CControllerDriver.hpp"
#include "VoodooI2CController.hpp"

#define readRegister(X) nub->readRegister(X)
#define writeRegister(X, Y) nub->writeRegister(X, Y)

#define super IOService
OSDefineMetaClassAndStructors(VoodooI2CControllerDriver, IOService);

/**
 Frees VoodooI2CControllerNub class - releases objects instantiated in `init`
 */

void VoodooI2CControllerDriver::free() {
    OSSafeReleaseNULL(device_nubs);

    IOFree(bus_device->acpi_config, sizeof(VoodooI2CControllerBusConfig));
    IOFree(bus_device, sizeof(VoodooI2CControllerBusDevice));

    super::free();
}

/**
 Requests the nub to fetch bus configuration values from the ACPI tables

 @return returns kIOReturnSuccess if all desired values were obtained,
         else returns kIOReturnNotFound if (some or all) configuration values
         are missing
 */

IOReturn VoodooI2CControllerDriver::getBusConfig() {
    bool error = false;

    bus_device->transaction_fifo_depth = 32;
    bus_device->receive_fifo_depth = 32;

    if (nub->getACPIParams((const char*)"SSCN", &bus_device->acpi_config->ss_hcnt, &bus_device->acpi_config->ss_lcnt, NULL) != kIOReturnSuccess)
        error = true;

    if (nub->getACPIParams((const char*)"FMCN", &bus_device->acpi_config->fs_hcnt, &bus_device->acpi_config->fs_lcnt, &bus_device->acpi_config->sda_hold) != kIOReturnSuccess)
        error = true;

    if (error)
        return kIOReturnNotFound;
    else
        return kIOReturnSuccess;
}

void VoodooI2CControllerDriver::handleAbortI2C() {
    IOLog("%s::%s I2C Transaction error details\n", getName(), bus_device->name);

    if (bus_device->abort_source & DW_IC_TX_ABRT_7B_ADDR_NOACK)
        IOLog("%s::%s slave address not acknowledged (7bit mode)\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_10ADDR1_NOACK)
        IOLog("%s::%s first address byte not acknowledged (10bit mode)\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_10ADDR2_NOACK)
        IOLog("%s::%s second address byte not acknowledged (10bit mode)\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_TXDATA_NOACK)
        IOLog("%s::%s data not acknowledged\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_GCALL_NOACK)
        IOLog("%s::%s no acknowledgement for a general call\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_GCALL_READ)
        IOLog("%s::%s read after general call\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_SBYTE_ACKDET)
        IOLog("%s::%s start byte acknowledged\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_SBYTE_NORSTRT)
        IOLog("%s::%s trying to send start byte when restart is disabled\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_10B_RD_NORSTRT)
        IOLog("%s::%s trying to read when restart is disabled (10bit mode)\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ABRT_MASTER_DIS)
        IOLog("%s::%s trying to use disabled adapter\n", getName(), bus_device->name);
    if (bus_device->abort_source & DW_IC_TX_ARB_LOST)
        IOLog("%s::%s lost arbitration\n", getName(), bus_device->name);

    IOLog("%s::%s I2C Transaction error: 0x%08x - aborting\n", getName(), bus_device->name, bus_device->abort_source);
}


/**
 Handles an interrupt that has been asserted by the controller
 */

void VoodooI2CControllerDriver::handleInterrupt() {
    UInt32 status, enabled;

    enabled = readRegister(DW_IC_ENABLE);
    status = readRegister(DW_IC_RAW_INTR_STAT);

    if (!enabled || !(status &~DW_IC_INTR_ACTIVITY))
        return;

    status = readClearInterruptBits();

    if (status & DW_IC_INTR_TX_ABRT) {
        bus_device->command_error |= DW_IC_ERR_TX_ABRT;
        bus_device->status = STATUS_IDLE;

        writeRegister(0, DW_IC_INTR_MASK);
        goto wakeup;
    }

    if (status & DW_IC_INTR_RX_FULL)
        readFromBus();

    if (status & DW_IC_INTR_TX_EMPTY)
        // transferToBus(_dev);

wakeup:
    if ((status & (DW_IC_INTR_TX_ABRT | DW_IC_INTR_STOP_DET)) || bus_device->message_error) {
        nub->command_gate->commandWakeup(&bus_device->command_complete);
    }
}

/**
 Initialises VoodooI2CControllerNub class
 
 @param properties OSDictionary* representing the matched personality
 
 @return returns true on successful initialisation, else returns false
 */

bool VoodooI2CControllerDriver::init(OSDictionary* properties) {
    if (!super::init(properties))
        return false;

    bus_device = reinterpret_cast<VoodooI2CControllerBusDevice*>(IOMalloc(sizeof(VoodooI2CControllerBusDevice)));
    bus_device->acpi_config = reinterpret_cast<VoodooI2CControllerBusConfig*>(IOMalloc(sizeof(VoodooI2CControllerBusConfig)));
    bus_device->awake = true;

    device_nubs = OSArray::withCapacity(1);

    return true;
}

/**
 Initialises the bus by writing in configuration values

 @return returns kIOReturnSuccess on successful initialisation, else returns kIOReturnError
 */

IOReturn VoodooI2CControllerDriver::initialiseBus() {
    if (toggleBusState(kVoodooI2CStateOff) != kIOReturnSuccess)
        return kIOReturnError;

    writeRegister(bus_device->acpi_config->ss_hcnt, DW_IC_SS_SCL_HCNT);
    writeRegister(bus_device->acpi_config->ss_lcnt, DW_IC_SS_SCL_LCNT);
    writeRegister(bus_device->acpi_config->fs_hcnt, DW_IC_FS_SCL_HCNT);
    writeRegister(bus_device->acpi_config->fs_lcnt, DW_IC_FS_SCL_LCNT);

    UInt32 reg = readRegister(DW_IC_COMP_VERSION);

    if  (reg >= DW_IC_SDA_HOLD_MIN_VERS)
        writeRegister(bus_device->acpi_config->sda_hold, DW_IC_SDA_HOLD);
    else
        IOLog("%s::%s Warning: hardware too old to adjust SDA hold time\n", getName(), bus_device->name);

    writeRegister(bus_device->transaction_fifo_depth - 1, DW_IC_TX_TL);
    writeRegister(0, DW_IC_RX_TL);
    writeRegister(bus_device->bus_config, DW_IC_CON);

    return kIOReturnSuccess;
}



/**
 Prepares the driver for an I2C transfer routine

 @param messages VoodooI2CControllerBusMessage* representing the messages to be transferred
 @param number   int* representing the number of messages

 @return returns kIOReturnSuccess on successful preparation, else returns kIOReturnTimeout
         or kIOReturnDeviceBusy
 */
IOReturn VoodooI2CControllerDriver::prepareTransferI2C(VoodooI2CControllerBusMessage* messages, int* number) {
    AbsoluteTime abstime;
    IOReturn sleep;

    bus_device->messages = messages;
    bus_device->message_number = *number;
    bus_device->command_error = 0;
    bus_device->message_write_index = 0;
    bus_device->message_read_index = 0;
    bus_device->message_error = 0;
    bus_device->status = STATUS_IDLE;
    bus_device->abort_source = 0;
    bus_device->receive_outstanding = 0;

    if (waitBusNotBusyI2C() != kIOReturnSuccess)
        return kIOReturnBusy;

    requestTransferI2C();

    nanoseconds_to_absolutetime(10000, &abstime);

    sleep = nub->command_gate->commandSleep(&bus_device->command_complete, abstime);

    if ( sleep == THREAD_TIMED_OUT ) {
        IOLog("%s::%s Timeout waiting for bus to accept transfer request\n", getName(), bus_device->name);
        initialiseBus();
        return kIOReturnTimeout;
    }

    /*
     * We must disable the adapter before returning and signaling the end
     * of the current transfer. Otherwise the hardware might continue
     * generating interrupts which in turn causes a race condition with
     * the following transfer.  Needs some more investigation if the
     * additional interrupts are a hardware bug or this driver doesn't
     * handle them correctly yet.
     */
    toggleBusState(kVoodooI2CStateOff);

    if (bus_device->message_error)
        return kIOReturnError;

    if (!bus_device->command_error)
        return kIOReturnError;

    if (bus_device->command_error == DW_IC_ERR_TX_ABRT) {
        handleAbortI2C();
        return kIOReturnError;
    }

    return kIOReturnSuccess;
}

/**
 Probes the device to determine whether or not we can drive it
 
 @param provider IOService* representing the matched entry in the IORegistry
 @param score    Probe score as specified in the matched personality
 
 @return returns a pointer to this instance of VoodooI2CControllerDriver
 */

VoodooI2CControllerDriver* VoodooI2CControllerDriver::probe(IOService* provider, SInt32* score) {
    UInt32 reg;

    if (!super::probe(provider, score)) {
        return NULL;
    }

    nub = OSDynamicCast(VoodooI2CControllerNub, provider);

    bus_device->name = nub->name;

    IOLog("%s::%s Probing controller\n", getName(), bus_device->name);

    reg = readRegister(DW_IC_COMP_TYPE);

    if (reg == DW_IC_COMP_TYPE_VALUE) {
        IOLog("%s::%s Found valid Synopsys component, continuing with initialisation\n", getName(), bus_device->name);
    } else {
        IOLog("%s::%s Unknown Synopsys component type: 0x%08x\n", getName(), bus_device->name, reg);
        return NULL;
    }

    return this;
}

/**
 Traverses the IOACPIPlane to find children and publishes `VoodooI2CDeviceNub` entries
 into the IORegistry for matching
 
 @return returns kIOReturnSuccess if successful, else kIOReturnError
 */

IOReturn VoodooI2CControllerDriver::publishNubs() {
    IOLog("%s::%s Publishing device nubs\n", getName(), bus_device->name);

    IOService* child;
    IORegistryIterator* children = IORegistryIterator::iterateOver(nub->controller->physical_device->acpi_device, gIOACPIPlane);

    if (children != 0) {
        OSOrderedSet* set = children->iterateAll();
        if (set != 0) {
            OSIterator *iterator = OSCollectionIterator::withCollection(set);
            if (iterator != 0) {
                while ((child = reinterpret_cast<IOService*>(iterator->getNextObject()))) {
                    IOLog("%s::%s Found I2C device: %s\n", getName(), bus_device->name, getMatchedName(child));

                    VoodooI2CDeviceNub* device_nub = OSTypeAlloc(VoodooI2CDeviceNub);

                    if (!device_nub->init(child->dictionaryWithProperties()) ||
                        !device_nub->attach(this, child) ||
                        !device_nub->start(this)) {
                        IOLog("%s::%s Could not initialise nub for %s\n", getName(), bus_device->name, getMatchedName(child));
                        OSSafeReleaseNULL(device_nub);
                        continue;
                    }
                    //device_nub->registerService();
                    device_nubs->setObject(device_nub);
                }
                iterator->release();
            }
            set->release();
        }
    }
    children->release();

    return kIOReturnSuccess;
}

/**
 Determines what type of interrupt has fired

 @return returns UInt32 representing the type of interrupt
 */

UInt32 VoodooI2CControllerDriver::readClearInterruptBits() {
    UInt32 stat;
    stat = readRegister(DW_IC_INTR_STAT);

    if (stat & DW_IC_INTR_RX_UNDER)
        readRegister(DW_IC_CLR_RX_UNDER);
    if (stat & DW_IC_INTR_RX_OVER)
        readRegister(DW_IC_CLR_RX_OVER);
    if (stat & DW_IC_INTR_TX_OVER)
        readRegister(DW_IC_CLR_TX_OVER);
    if (stat & DW_IC_INTR_RD_REQ)
        readRegister(DW_IC_CLR_RD_REQ);
    if (stat & DW_IC_INTR_TX_ABRT) {
        bus_device->abort_source = readRegister(DW_IC_TX_ABRT_SOURCE);
        readRegister(DW_IC_CLR_TX_ABRT);
    }
    if (stat & DW_IC_INTR_RX_DONE)
        readRegister(DW_IC_CLR_RX_DONE);
    if (stat & DW_IC_INTR_ACTIVITY)
        readRegister(DW_IC_CLR_ACTIVITY);
    if (stat & DW_IC_INTR_STOP_DET)
        readRegister(DW_IC_CLR_STOP_DET);
    if (stat & DW_IC_INTR_START_DET)
        readRegister(DW_IC_CLR_START_DET);
    if (stat & DW_IC_INTR_GEN_CALL)
        readRegister(DW_IC_CLR_GEN_CALL);

    return stat;
}

void VoodooI2CControllerDriver::readFromBus() {
    VoodooI2CControllerBusMessage *messages = bus_device->messages;
    int receive_valid;

    for (; bus_device->message_read_index < bus_device->message_number; bus_device->message_read_index++) {
        UInt32 length;
        UInt8 *buffer;

        /** if current message is not a read, skip */
        if (!(messages[bus_device->message_read_index].flags & I2C_M_RD))
            continue;

        /** if a read is not in progress then take the length and the current message in the loop
            else just set the length and buffer to the previous length and buffer */
        if (!(bus_device->status & STATUS_READ_IN_PROGRESS)) {
            length = messages[bus_device->message_read_index].length;
            buffer = messages[bus_device->message_read_index].buffer;
        } else {
            length = bus_device->receive_buffer_length;
            buffer = bus_device->receive_buffer;
        }

        /** check how many items left in receive buffer */
        receive_valid = readRegister(DW_IC_RXFLR);

        /** collect data from receive buffer */
        for (; length > 0 && receive_valid > 0; length--, receive_valid--)
            *buffer++ = readRegister(DW_IC_DATA_CMD);
        bus_device->receive_outstanding--;

        /** if there are still more messages to read, set status to read in progress and continue
            else remove read in progress status */
        if (length > 0) {
            bus_device->status |= STATUS_READ_IN_PROGRESS;
            bus_device->receive_buffer_length = length;
            bus_device->receive_buffer = buffer;
            return;
        } else {
            bus_device->status &= ~STATUS_READ_IN_PROGRESS;
        }
    }
}

void VoodooI2CControllerDriver::requestTransferI2C() {
    VoodooI2CControllerBusMessage *messages = bus_device->messages;
    UInt32 i2c_configuration, i2c_target = 0;

    toggleBusState(kVoodooI2CStateOff);

    /* if the slave address is ten bit address, enable 10BITADDR */
    i2c_configuration = readRegister(DW_IC_CON);
    if (messages[bus_device->message_write_index].flags & I2C_M_TEN) {
        i2c_configuration |= DW_IC_CON_10BITADDR_MASTER;
        /*
         * If I2C_DYNAMIC_TAR_UPDATE is set, the 10-bit addressing
         * mode has to be enabled via bit 12 of IC_TAR register.
         * We set it always as I2C_DYNAMIC_TAR_UPDATE can't be
         * detected from registers.
         */
        i2c_target = DW_IC_TAR_10BITADDR_MASTER;
    } else {
        i2c_configuration &= ~DW_IC_CON_10BITADDR_MASTER;
    }

    writeRegister(i2c_configuration, DW_IC_CON);

    /*
     * Set the slave (target) address and enable 10-bit addressing mode
     * if applicable.
     */
    writeRegister(messages[bus_device->message_write_index].address | i2c_target, DW_IC_TAR);

    toggleInterrupts(kVoodooI2CStateOff);

    toggleBusState(kVoodooI2CStateOn);

    toggleInterrupts(kVoodooI2CStateOn);
}

/**
 Called by the system's power manager to set power states
 
 @param whichState either kIOPMPowerOff or kIOPMPowerOn
 @param whatDevice Power management policy maker
 
 @return returns kIOPMAckImplied if power state has been set else maximum number of milliseconds needed for the device to be in the correct state
 */

IOReturn VoodooI2CControllerDriver::setPowerState(unsigned long whichState, IOService *whatDevice) {
    if (!whichState) {
        bus_device->awake = false;
        toggleBusState(kVoodooI2CStateOff);
        IOLog("%s::%s Going to sleep\n", getName(), bus_device->name);
    } else {
        if (!bus_device->awake) {
            toggleBusState(kVoodooI2CStateOn);
            initialiseBus();
            toggleInterrupts(kVoodooI2CStateOff);
            bus_device->awake = true;
            IOLog("%s::%s Woke up\n", getName(), bus_device->name);
        }
    }
    return kIOPMAckImplied;
}

/**
 Starts the bus
 
 @param provider IOService* representing the nub
 
 @return returns true on succesful start, else returns false
 */

bool VoodooI2CControllerDriver::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    PMinit();
    nub->joinPMtree(this);
    registerPowerDriver(this, VoodooI2CIOPMPowerStates, kVoodooI2CIOPMNumberPowerStates);

    if (getBusConfig() != kIOReturnSuccess)
        IOLog("%s::%s Warning: Error getting bus config, using defaults where necessary\n", getName(), bus_device->name);
    else
        IOLog("%s::%s Got bus configuration values\n", getName(), bus_device->name);

    bus_device->functionality = I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK;
    bus_device->bus_config = DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE | DW_IC_CON_RESTART_EN | DW_IC_CON_SPEED_FAST;

    if (initialiseBus() != kIOReturnSuccess) {
        IOLog("%s::%s Could not initialise bus\n", getName(), bus_device->name);
        return false;
    }

    toggleInterrupts(kVoodooI2CStateOff);

    publishNubs();

    return true;
}

/**
 Stops the bus and undoes the effects of `start` and `probe`
 
 @param provider IOService* representing the matched entry in the IORegistry
 */

void VoodooI2CControllerDriver::stop(IOService* provider) {
    while (device_nubs->getCount() > 0){
        VoodooI2CDeviceNub *nub = (VoodooI2CDeviceNub *)device_nubs->getLastObject();
        nub->detach(this);
        device_nubs->removeObject(device_nubs->getCount() - 1);
        OSSafeReleaseNULL(nub);
    }
    
    toggleBusState(kVoodooI2CStateOn);

    PMstop();

    super::stop(provider);
}

/**
 Toggle the bus's enabled state
 
 @param enabled VoodooI2CPowerState representing the enabled state
 
 @return returns kIOReturnSuccess on successful state toggle, else returns kIOReturnTimeout
 */

IOReturn VoodooI2CControllerDriver::toggleBusState(VoodooI2CState enabled) {
    int timeout = 500;

    do {
        writeRegister(enabled, DW_IC_ENABLE);

        if ((readRegister(DW_IC_ENABLE_STATUS) & 1) == enabled) {
            toggleClockGating(enabled);
            return kIOReturnSuccess;
        }

        IODelay(250);
    } while (timeout--);

    IOLog("%s::%s Timed out waiting for bus to change state\n", getName(), bus_device->name);
    return kIOReturnTimeout;
}

/**
 Toggle clock gating to save power

 @param enabled VoodooI2CPowerState representing the state of the clock gating
 */

inline void VoodooI2CControllerDriver::toggleClockGating(VoodooI2CState enabled) {
    writeRegister(enabled, LPSS_PRIVATE_CLOCK_GATING);
}

/**
 Toggle interrupts by masking

 @param enabled VoodooI2CPowerState representing state of the masking
 */
void VoodooI2CControllerDriver::toggleInterrupts(VoodooI2CState enabled) {
    if (!enabled) {
        writeRegister(0, DW_IC_INTR_MASK);
    } else {
        readRegister(DW_IC_CLR_INTR);
        writeRegister(DW_IC_INTR_DEFAULT_MASK, DW_IC_INTR_MASK);
    }
}

/**
 Directs the command gate to add an I2C transfer routine to the work loop

 @param messages VoodooI2CControllerBusMessage* representing the messages to be transferred
 @param number   int representing the number of messages

 @return returns kIOReturnSuccess on successful
 */
IOReturn VoodooI2CControllerDriver::transferI2C(VoodooI2CControllerBusMessage* messages, int number) {
    return nub->command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooI2CControllerDriver::transferI2CGated), messages, &number);
}


/**
 Attempts an I2C transfer routine

 @param messages VoodooI2CControllerBusMessage* representing the messages to be transferred
 @param number   int representing the number of messages

 @return returns kIOReturnSuccess on successful
 */

IOReturn VoodooI2CControllerDriver::transferI2CGated(VoodooI2CControllerBusMessage* messages, int* number) {
    IOReturn ret;
    int tries;

    for (ret = 0, tries = 0; tries <= 5; tries++) {
        ret = prepareTransferI2C(messages, number);
        if (ret != kIOReturnSuccess)
            break;
    }

    return ret;
}

/**
 Transfers an I2C protocol message to the bus
 */

void VoodooI2CControllerDriver::transferMessageToBus() {
    VoodooI2CControllerBusMessage *messages = bus_device->messages;
    UInt32 interrupt_mask;
    int transaction_limit, receive_limit;
    UInt32 address = messages[bus_device->message_write_index].address;
    UInt32 buffer_length = bus_device->transaction_buffer_length;
    UInt8 *buffer = bus_device->transaction_buffer;
    bool need_restart = false;

    interrupt_mask = DW_IC_INTR_DEFAULT_MASK;

    for (; bus_device->message_write_index < bus_device->message_number; bus_device->message_write_index++) {
        /*
         * if target address has changed, we need to
         * reprogram the target address in the i2c
         * adapter when we are done with this transfer
         */
        if (messages[bus_device->message_write_index].address != address) {
            bus_device->message_error = -1;
            break;
        }

        if (messages[bus_device->message_write_index].length == 0) {
            bus_device->message_error = -1;
            break;
        }

        if (!(bus_device->status & STATUS_WRITE_IN_PROGRESS)) {
            buffer = messages[bus_device->message_write_index].buffer;
            buffer_length = messages[bus_device->message_write_index].length;

            /* If both IC_EMPTYFIFO_HOLD_MASTER_EN and
             * IC_RESTART_EN are set, we must manually
             * set restart bit between messages.
             */
            if ((bus_device->bus_config & DW_IC_CON_RESTART_EN) && (bus_device->message_write_index > 0)) {
                need_restart = true;
            }
        }

        transaction_limit = bus_device->transaction_fifo_depth - readRegister(DW_IC_TXFLR);
        receive_limit = bus_device->receive_fifo_depth - readRegister(DW_IC_RXFLR);

        while (buffer_length > 0 && transaction_limit > 0 && receive_limit > 0) {
            UInt32 command = 0;

            /*
             * If IC_EMPTYFIFO_HOLD_MASTER_EN is set we must
             * manually set the stop bit. However, it cannot be
             * detected from the registers so we set it always
             * when writing/reading the last byte.
             */

            if (bus_device->message_write_index == bus_device->message_number - 1 && buffer_length == 1) {
                command |= 0x200;
            }

            if (need_restart) {
                command |= 0x400;
                need_restart = false;
            }
            if (messages[bus_device->message_write_index].flags & I2C_M_RD) {
                /* avoid rx buffer overrun */
                if (receive_limit - bus_device->receive_outstanding <= 0) {
                    break;
                }
                writeRegister(command | 0x100, DW_IC_DATA_CMD);
                receive_limit--;
                bus_device->receive_outstanding++;
            } else {
                writeRegister(command | *buffer++, DW_IC_DATA_CMD);
            }
            transaction_limit--; buffer_length--;
        }

        bus_device->transaction_buffer = buffer;
        bus_device->transaction_buffer_length = buffer_length;

        if (buffer_length > 0) {
            bus_device->status |= STATUS_WRITE_IN_PROGRESS;
            break;
        } else {
            bus_device->status &= ~STATUS_WRITE_IN_PROGRESS;
        }
    }

    /*
     * If i2c_msg index search is completed, we don't need TX_EMPTY
     * interrupt any more.
     */
    if (bus_device->message_write_index == bus_device->message_number) {
        interrupt_mask &= ~DW_IC_INTR_TX_EMPTY;
    }

    if (bus_device->message_error) {
        interrupt_mask = 0;
    }

    writeRegister(interrupt_mask, DW_IC_INTR_MASK);
}

IOReturn VoodooI2CControllerDriver::waitBusNotBusyI2C() {
    int timeout = TIMEOUT * 100;

    while (readRegister(DW_IC_STATUS) & DW_IC_STATUS_ACTIVITY) {
        if (timeout <= 0) {
            IOLog("%s::%s Warning: Timeout waiting for bus not to be busy\n", getName(), bus_device->name);
            return kIOReturnBusy;
        }
        timeout--;

        IODelay(1100);
    }

    return kIOReturnSuccess;
}
