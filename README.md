Introduction
====================
TBD.

Prerequisites
====================
-   [bash](https://www.gnu.org/software/bash/)
-   [Boost](http://www.boost.org/)
-   [Clang](https://clang.llvm.org) >= 5.0.0, < 6
-   [libclc](https://libclc.llvm.org)
-   (Optional) [LLD](https://lld.llvm.org/)
-   [libsystemd](https://github.com/systemd/systemd) >= 221
-   [yaml-cpp](https://github.com/jbeder/yaml-cpp) = 0.5.3
-   OpenCL 1.2 compatible runtime
-   C++17 compliant toolchain (e.g. GCC >= 7.1.0, Clang >= 5.0.0)

Building
====================

Install prerequisites
--------------------
Arch Linux:

	# pacman -S libclc yaml-cpp

Building patched clang
--------------------
Fetch the source:

```
$ svn export http://llvm.org/svn/llvm-project/llvm/tags/RELEASE_500/final <llvm-src-dir>
$ svn export http://llvm.org/svn/llvm-project/cfe/tags/RELEASE_500/final <llvm-src-dir>/tools/clang
$ svn export http://llvm.org/svn/llvm-project/compiler-rt/tags/RELEASE_500/final <llvm-src-dir>/projects/compiler-rt
$ svn export http://llvm.org/svn/llvm-project/clang-tools-extra/tags/RELEASE_500/final <llvm-src-dir>/tools/clang/tools/extra
```

Patch Clang:

```
$ cd <llvm-src-dir>/tools/clang
$ patch -p1 < <CLPKM-src-dir>/clang500.patch
```

Build Clang:

```
$ cd ../..
$ mkdir build && cd build
$ cmake -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_USE_LINKER=lld \
    -DCMAKE_INSTALL_PREFIX=<llvm-install-dir> \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_BUILD_LLVM_DYLIB=ON \
    -DLLVM_LINK_LLVM_DYLIB=ON \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    ..
$ make -j 24 install
```

Building CLPKM
--------------------

```
$ cd ../..
$ cd <CLPKM-src-dir>
$ cd inliner && make LLVM_CONFIG=<llvm-install-dir>/bin/llvm-config -j 24
$ cd ../rename-lst-gen && make LLVM_CONFIG=<llvm-install-dir>/bin/llvm-config -j 24
$ cd ../cc && make LLVM_CONFIG=<llvm-install-dir>/bin/llvm-config -j 24
$ cd ../daemon && make -j 24
$ cd ../runtime && make -j 24
```

Setup configurations
--------------------

```
cd ..

# Change the path of tool to the llvm we just built
vim clpkm.sh

# Create a new configuration
vim clpkm.conf
```

configuration example:

```
---
compiler:  /home/tock/CLPKM/clpkm.sh
threshold: 100000
...
```

Using CLPKM
====================
Start the daemon first, for example run it on the terminal, user bus:

	<CLPKM-src-dir>/clpkm-daemon terminal user <CLPKM-src-dir>/clpkm.conf

Run a OpenCL application as a low priority process, with diagnostic info:

	env CLPKM_PRIORITY=low CLPKM_LOGLEVEL=debug LD_PRELOAD=<CLPKM-src-dir>/runtime/libclpkm.so <command-to-run-ocl-app>

Run a application as high priority task:

	env CLPKM_PRIORITY=high LD_PRELOAD=<CLPKM-src-dir>/runtime/libclpkm.so <command-to-run-ocl-app>
