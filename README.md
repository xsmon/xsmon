# xsmon

Extra small system monitor for X11.

[![Build Status](https://travis-ci.com/xsmon/xsmon.svg)](https://travis-ci.com/xsmon/xsmon)

## Screenshot

![screenshot](/screen.gif?raw=true)

## Features

* CPU and RAM usage monitoring
* alerting when threshold exceeded
* customizable colors
* extra low memory footprint
* zero dependency (only XCB)

## Build Instructions

### DEB-based distro

```sh
apt install libx11-xcb-dev
```

### RPM-based distro

```sh
yum install libxcb-devel
```

### Build & Run

```sh
make
./xsmon
```

## License
[MIT](/LICENSE.MIT)
