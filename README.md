YSoX
====

YSoX is a [Yorick](http://yorick.github.com/) plug-in for endocing/decoding
audio thanks to the [SoX library](http://sox.sourceforge.net/).

Installation
------------

In short, building and installing the plug-in can be as quick as:
````
$ cd $BUILD_DIR
$ $SRC_DIR/configure
$ make
$ make install
````
where `$BUILD_DIR` is the build directory (at your convenience) and
`$SRC\_DIR` is the source directory of the plug-in code.  The build and
source directories can be the same in which case, call `./configure` to
configure for building.

Then, to use the plug-in in Yorick:
````
$ yorick
> include "sox.i"
````
More detailled explanations are given below.

1. You must have Yorick and the SoX library installed on your machine.

2. Unpack the plug-in code somewhere.

3. Configure for compilation.  The are two possibilities:

   For an **in-place build**, go to the source directory of the plug-in
   code and run the configuration script:
   ````
   $ cd SRC_DIR
   $ ./configure
   ````
   To see the configuration options, call:
   ````
   $ ./configure --help
   ````

   To compile in a **different build directory**, say BUILD_DIR, create the
   build directory, go to the build directory, and run the configuration
   script:
   ````
   $ mkdir -p $BUILD_DIR
   $ cd $BUILD_DIR
   $ $SRC_DIR/configure
   ````
   where `$SRC_DIR` is the path to the source directory of the plug-in
   code. To see the configuration options, call:
   ````
   $ $SRC_DIR/configure --help
   ````

4. Compile the code:
   ````
   $ make
   ````

4. Install the plug-in in Yorick directories:
   ````
   $ make install
   ````


License
-------

**YSoX** is open source sofware released under the [MIT license](LICENSE.md).

To temporarily suspend **SIGFPE** (floating-point error) signals, the
`switch_fpemask()` function is based on `ygl_fpemask()` borrowed from
[Yorick-GL](https://github.com/dhmunro/yorick-gl) plug-in written by Dave
Munro.  For this piece of code, the following applies:

> Copyright (c) 2012, David H. Munro.
> All rights reserved.
> This file is part of yorick (http://yorick.sourceforge.net).
> Read the accompanying LICENSE file for details.
