#<cldoc:Introduction>

&#8291;

## What is VoodooI2C?

VoodooI2C is a project consisting of macOS kernel extensions that add support for I2C bus devices. The project is split into two main components: the **core** extension and various other **satellite** extensions.

### The Core

The core is the `VoodooI2C.kext` kernel extension. This kext is intended to be installed by anyone whose computer requires some form of I2C support. It consists of I2C controller drivers and is responsible for publishing device nubs to the IOService plane.

### The Satellites

The satellites are a collection of various kernel extensions that implement support for a specific type of I2C device. An example of a satellite kext is `VoodooI2CHID.kext` which adds support for I2C-HID devices. Usually a user will install one satellite kext per class of I2C device.

## Current Status

The following Intel I2C controllers are fully supported:

1. `INT33C2` and `INT33C3` - Haswell era
2. `INT3432` and `INT3433` - Broadwell era
3. `pci8086,9d60`, `pci8086,9d61`, `pci8086,a160` and `pci8086,a161` - Skylake/Kabylake era

The following device classes are fully supported:

1. I2C-HID devices
2. ELAN devices

Note that there is sometimes an overlap between device classes. For example, some ELAN devices may also be I2C-HID devices.

## Releases

The latest version is **v2.0.0** and can be downloaded on the [release page](https://github.com/alexandred/VoodooI2C/releases).

## Compatibility

Please check the [compatibility page](https://github.com/alexandred/VoodooI2C/wiki/Compatibility) on the VoodooI2C wiki to find out if your device is compatible. If it is not on the list but you still suspect VoodooI2C may work for you, contact us on our [Gitter page](http://gitter.im/alexandred/VoodooI2C).

## Documentation and Troubleshooting

Please visit the [documentation site](https://alexandred.github.io/VoodooI2C) for further information how to install and troubleshoot VoodooI2C. 

## Helping out and testing

We are always looking for people with computers that have I2C controllers to help ensure these drivers will work for them.

The first step to setting up debugging is to open up IORegExplorer (v2.1) and search for controller ID relevant to your platform (see the **Current Status** section above). If you find something, great! Contact us on our [Gitter page](http://gitter.im/alexandred/VoodooI2C) and we'll see what kind of support your device will have.

If not (and you still suspect that you have I2C devices), you may need to patch your DSDT to ensure that OS X can properly enumerate the devices. You can see the wiki for instructions on how to do this.

## License

This program is protected by the GPL license. Please refer to the `LICENSE.txt` file for more information

## Contributing

We are looking for competent C++, OS X kernel, Linux kernel, I2C, HID etc developers to help improve this project! Here are the guidelines for contributing:

* Fork this repository and clone to your local machine
* Create a new feature branch and add commits
* Push your feature branch to your fork
* Submit a pull request upstream

## Donations

Donations can be made via Bitcoin to the following wallet: https://live.blockcypher.com/btc/address/1EUxSExh8XCveWxVDVQqnez2o95AnG8Qhr/ .
