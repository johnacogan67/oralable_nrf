# tgm_firmware

Firmware for the TGM

# nRF Connect SDK example application

This repository is based on [ncs-example-application](https://nrfconnect.github.io/ncs-example-application)
which contains an nRF Connect SDK example application. The main
purpose of this repository is to serve as a reference on how to structure nRF Connect
SDK based applications. Please refer to the documentation in that repository for information related to the structuring of this repo.

## Getting started

Before getting started, make sure you have a proper nRF Connect SDK development environment.
Follow the official
[Installation guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation/install_ncs.html).

### Initialization

The first step is to initialize the workspace folder (`tgm_firmware`) where
the `tgm_firmware` and all nRF Connect SDK modules will be cloned. Run the following
command:

```shell
# initialize tgm_firmware workspace for the tgm_firmware application (main branch)
west init -m https://github.com/johna67/tgm_firmware --mr main tgm_firmware
# update nRF Connect SDK modules
cd tgm_firmware
west update
```

### Building and running

The easiest way to build and run the example is using the nRF Connect extension in VSCode.
When freshly installing this repo, you will need to 'Add existing application' and select the tgm_firmware/app directory.
Next, add a build configuration, selecting the board you want to build for.
Check the boards directory for a selection of supported boards

Give the 'Build directory name' a good name to distinguish different builds (e.g. add board as a suffix)
Ensure that 'Build after generating configuration' is checked and click on 'Build configuration'

For future builds, you can use the 'Build' action in the nRF Connect pane.
Debugging (make sure to select Optimization level 'Optimize for debugging -Og')) and Flashing a device can also be done through here

### Streaming PPG data

The device will stream PPG data (Red, IR and Green) with each Bluetooth package containing CONFIG_PPG_SAMPLES_PER_FRAME (to be set in the application prj.conf file)

#### Parsing the PPG data

The PPG data is currently sampled at 50Hz (default). The amount of samples per data frame is configurable as the prj.conf file with the parameter CONFIG_PPG_SAMPLES_PER_FRAME. This is currently set at 20, meaning there should be a frame every 0.4 sec.
Each frame is built up as follows as structure of type tgm_service_ppg_data_t (see tgm_service.h):

- Bytes 0-4: frame counter, this increments with every frame
- Bytes 4-16: sample 1 of frame
  - Bytes 4-8: red sample
  - Bytes 8-12: IR sample
  - Bytes 12-16: Green sample
- Bytes 16-28: sample 2 of frame
  ...

### Tuning the PPG sensor parameters

Currently the TGM service supports modifying the MAXM86161 registers on the go to allow for easy tuning of the parameters.
To modify a PPG sensor register, write 2 bytes to the last characteristic (3a0ff008-...), the first one indicating which register to modify and the second one containing the value you want to write to the register.

Example:

- To modify the strength of the IR LED, write 0x24hh with hh the value of the register (in hex)
- To modify the strength of the Red LED, write 0x25hh with hh the value of the register (in hex)

### Streaming accelerometer data

The device will stream accelerometer data (x, y and z) with each Bluetooth package containing CONFIG_ACC_SAMPLES_PER_FRAME (to be set in the application prj.conf file)

#### Parsing the accelerometer data

The accelerometer data is currently sampled at 50Hz (default). The amount of samples per data frame is configurable as the prj.conf file with the parameter CONFIG_ACC_SAMPLES_PER_FRAME. This is currently set at 25, meaning there should be a frame every 0.5 sec.
Each frame is built up as follows as structure of type tgm_service_acc_data_t (see tgm_service.h):

- Bytes 0-4: frame counter, this increments with every frame
- Bytes 4-10: sample 1 of frame
  - Bytes 4-6: x sample
  - Bytes 6-8: y sample
  - Bytes 8-10: z sample
- Bytes 10-16: sample 2 of frame
  ...

### Streaming battery voltage

The device will stream battery voltage data at an interval defined by CONFIG_BATTERY_MEASUREMENT_INTERVAL. The default value is set to 300 seconds (5 minutes), but this value can be modified based on requirements

#### Parsing the battery voltage data

The battery voltage is sent as a int32_t, which means that 4 bytes are sent with every measurement event. The value of the battery voltage is reported in mV.

### Streaming temperature

The device will stream the temperature of the BLE module at an interval defined by CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL. The default value is set to 1 second, but this value can be modified based on requirements.

### Parsing the temperature

Each frame consists of 8 bytes. The first 4 bytes indicate the frame counter and bytes 5-6 encode the temperature as a signed integer in centidegree Celsius unit (1/100 of a degree), so the value of 2137 is equal to 21.37°C.
