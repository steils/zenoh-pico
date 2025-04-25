<img src="https://raw.githubusercontent.com/eclipse-zenoh/zenoh/master/zenoh-dragon.png" height="150">

# Zetta Zenoh-Pico - QNX Support

To compile Zenoh-Pico for QNX 7.1.0 a QNX development environment is required.

## Supported Architectures

Currently, zenoh-pico provides support for the following QNX architectures:

|  **Architecture**     |        **Transport Layer**       |  **Network Layer**  |                 **Data Link Layer**                |
|:---------------------:|:--------------------------------:|:-------------------:|:--------------------------------------------------:|
|      **x86_64**       | UDP (unicast and multicast), TCP |         IPv4        |                      Ethernet                      |
|     **aarch64le**     | UDP (unicast and multicast), TCP |         IPv4        |                      Ethernet                      |

## Building Zenoh-Pico for QNX

The following steps describe how to compile Zenoh-Pico for QNX:

1. Setup the QNX environment:

```bash
source ~/qnx710/qnxsdp.sh
```

2a. Build for a QNX 7.1.0 x86_64 target:

```bash
make CMAKE_TOOLCHAIN_FILE=./ports/qnx/qnx-sdp710-x86_64.cmake
```

2b. Alternatively build for a QNX 7.1.0 aarch64le target:

```bash
make CMAKE_TOOLCHAIN_FILE=./ports/qnx/qnx-sdp710-aarch64le.cmake
```
