# Microsoft Cognitive Services - Voice Assistant Sample Code C++ console GGEC Speaker setup

## Overview

This readme should go over setting up a Windows dev box to build an arm32 binary using a docker container for the GGEC speaker. You could build it on a Linux machine but the scripts provided are for Windows.

## Setting up the device

You will need the Android Debug Bridge (adb) which can be found [here](https://developer.android.com/studio/releases/platform-tools).

An unboxed GGEC device will have a hidden USB port between the AUX out and power input connections points. You have to peel off the adhered label to reveal the USB port.

### Setting up the WiFi

Open a command prompt and run the following commands. The first one will open an adb shell to the speaker.

  ```sh
  adb shell
  export $(cat /tmp/dbus-session)
  adk-message-send 'connectivity_wifi_onboard {}'
  ```  

Wait until the LED ring glows red, then replace your own WiFi's NETWORK-NAME and PASSWORD in the next command.

  ```sh
  adk-message-send 'connectivity_wifi_connect {ssid:"NETWORK-NAME" password:"PASSWORD" homeap:true}'
  ```  

Wait until the LED ring glows green.

  ```sh
  adk-message-send 'connectivity_wifi_completeonboarding {}'
  ```  

## Setting up the code

The repo should be cloned onto your dev machine and we will operate out of the cpp-console folder

To utilize the audio processing from the Microsoft Audio Stack, we will also need the specific binaries for the GGEC speaker that can be found here: [binaries](https://aka.ms/sdsdk-download). Those binaries should be copied to the lib folder in this repo so that you have a structure like this: ./lib/arm32

Download the Speech SDK: [Linux Speech SDK](https://aka.ms/csspeech/linuxbinary). The headers and their folders should be copied into the include folder so that you have a structure like this: ./include/cxx_api and ./include/c_api also copy the libaries to ./lib/arm32

Replace the text in the configs/config.json file with your subscription key and key region. If you are using a Custom Commands application or a Custom Voice insert those GUID's as well. The keyword_model should point to the Custom Keyword (.table file) being used.

## Building for Linux Arm32 with Docker

The building of the image will use docker which can be installed on Windows or Linux.
The building uses the working directory cpp-console\docker

### Using a Windows machine

Install docker for windows from the [docker website](https://docs.docker.com/docker-for-windows/).
For the build script to work the local drive needs to be shared to docker. See Settings - Resources - File Sharing.

### Using a Linux machine

Install docker, and also run

```sh
sudo apt-get install --yes binfmt-support qemu-user-static
```

### Download the ARM emulator

Download the qemu-arm-static.tar.gz file from this [open source](https://github.com/multiarch/qemu-user-static/releases/) and place the qemu-arm-static.tar.gz file in the cpp-console\docker folder. This is the arm emulator that the container will use.

## Build the image

Open a cmd prompt in the cpp-console\docker folder then run the docker image build script. This will create a docker image and name it "dev_ubuntu_arm32".

```sh
docker build -t dev_ubuntu_arm32 .
```

Then cd into the scripts\GGEC folder and run the actual build script. The output executable will be placed in the out folder.

```sh
.\buildGGEC.bat
```

### Deploy the sample

The script to copy the sample to the device is also in the scripts\GGEC folder

```sh
.\deployGGEC.bat
```

This will deploy all the configs, models, and binaries you will need along with the run.sh script into the /data/cppSample folder on your device.

## Running the sample

### usage: run.sh config-file
example running from the /data/cppSample folder:
    
    ./run.sh config.json
    
## Setting up the sample to run as a service

This can be useful if you want your speaker to start the sample automatically on boot and have it automatically restart if it fails.

cd into the scripts/GGEC directory

Change the startService.sh file to use the config.json file you would like. Then run

    .\deployService.bat

Then reboot your device.

## Troubleshooting

### Error details: 2460

This is a TLS certificate issue, a workaround is below

```sh
cd /usr/lib/ssl/certs
c_rehash
```

#### [Main Devices Readme](README.md)
