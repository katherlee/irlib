irlib
======
Python library for generating "intermediate-representation" (ir) basis.
Please refer to H. Shinaoka, J. Otsuki, M. Ohzeki and K. Yoshimi, PRB 96, 035147 (2017).
The basis functions u_l(x) and v_l(y) are orthonormal on the interval [-1,1] with weight 1.
The sign of u_l(x) is chosen so that u_l(1) > 0.


# Table of Contents
- [Requirements](#requirements)
- [Installation](#installation)
- [Examples](#examples)

## Requirements
### C++11 compiler

### MPFR (>= 2.31)

### GMP (>= 4.21)

### Python 2.7 or 3.x
For building Python binding.

### SWIG (>= 3.0)
For building Python binding

## Installation
See the following sample for a build.
In this case, Python libraries and unit tests are built.

```
$ cmake \
       -DCMAKE_CXX_FLAGS="-std=c++11" \
       -DCMAKE_INSTALL_PREFIX=$HOME/local \
       -DCMAKE_CXX_COMPILER=clang++ \
       -DTesting=ON \
       path_to_source_file_directory
$ make
$ make test
$ make install
```

## Contributors
Hiroshi Shinaoka, Naoya Chikano, Junya Otsuki

## Samples
You can find samples below the directory "samples" in the source directory.

## License
All files except for those in the directory "thirdparty" are licensed under MIT license. See LICENSE for more details.

## Trouble shooting
In case you have multiple Python installations on your machine, cmake may pick up a wrong one.
To force cmake to use the correct one,
you can tell the location of the executable like this.

```
$ cmake -DPYTHON_EXECUTABLE=/usr/local/bin/python3.6 ...(other options)...
```

Even when the location of the executable is specified, cmake may still pick up wrong Python libraries and header files.
In such a case, you must pass the include directory and the location of the Python library to cmake as follows.

```
$ cmake \
     -DPYTHON_EXECUTABLE=/usr/local/bin/python3.6 \
     -DPYTHON_INCLUDE_DIR=/usr/local/Frameworks/Python.framework/Versions/3.6/include/python3.6m \
     -DPYTHON_LIBRARY=/usr/local/Frameworks/Python.framework/Versions/3.6/lib/libpython3.6m.dylib \
     ... (other options) ...
```

If you want to force CMake not to find MPFR in the system directory,
please set the following two environment variables.

```
export MPFR_INCLUDE_DIR=$HOME/opt/mpfr/include
export MPFR_LIB_DIR=$HOME/opt/mpfr/lib
```
