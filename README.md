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

	# pacman -S boost libclc yaml-cpp

Building patched clang
--------------------
Setup path:

```
$ export LLVM_SRC_DIR="$HOME"/llvm-5.0.0-src
$ export LLVM_INSTALL_DIR="$HOME"/llvm-5.0.0
$ export CLPKM_SRC_DIR="$HOME"/CLPKM
```

Fetch the source:

```
$ svn export http://llvm.org/svn/llvm-project/llvm/tags/RELEASE_500/final "$LLVM_SRC_DIR"
$ svn export http://llvm.org/svn/llvm-project/cfe/tags/RELEASE_500/final "$LLVM_SRC_DIR"/tools/clang
$ svn export http://llvm.org/svn/llvm-project/compiler-rt/tags/RELEASE_500/final "$LLVM_SRC_DIR"/projects/compiler-rt
$ svn export http://llvm.org/svn/llvm-project/clang-tools-extra/tags/RELEASE_500/final "$LLVM_SRC_DIR"/tools/clang/tools/extra
```

Patch Clang:

```
$ cd "$LLVM_SRC_DIR"/tools/clang
$ patch -p1 < "$CLPKM_SRC_DIR"/clang500.patch
```

Build Clang:

```
$ cd ../..
$ mkdir build && cd build
$ cmake -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_USE_LINKER=lld \
    -DCMAKE_INSTALL_PREFIX="$LLVM_INSTALL_DIR" \
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
$ git clone --depth 1 https://<user-name>@bitbucket.org/TOCK-Chiu/CLPKM.git "$CLPKM_SRC_DIR"
$ cd "$CLPKM_SRC_DIR"
$ cd inliner && make LLVM_CONFIG="$LLVM_INSTALL_DIR"/bin/llvm-config -j 24
$ cd ../rename-lst-gen && make LLVM_CONFIG="$LLVM_INSTALL_DIR"/bin/llvm-config -j 24
$ cd ../cc && make LLVM_CONFIG="$LLVM_INSTALL_DIR"/bin/llvm-config -j 24
$ cd ../daemon && make -j 24
$ cd ../runtime && make -j 24
```

Setup configurations
--------------------
Change the path of tool to the llvm we just built:

```
$ cd ..
$ vim clpkm.sh
```

Create a new configuration:

	$ vim clpkm.conf

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

	$ "$CLPKM_SRC_DIR"/clpkm-daemon terminal user "$CLPKM_SRC_DIR"/clpkm.conf

(Note: It's quite likely that you need special configuration to make the daemon run on the system bus. You may want to check out /etc/dbus-1/system.d/avahi-dbus.conf.)

Run a OpenCL application as a low priority process, with diagnostic output enabled:

	$ env CLPKM_PRIORITY=low CLPKM_LOGLEVEL=debug LD_PRELOAD="$CLPKM_SRC_DIR"/runtime/libclpkm.so <command-to-run-ocl-app>

Run a application as high priority task:

	$ env CLPKM_PRIORITY=high LD_PRELOAD="$CLPKM_SRC_DIR"/runtime/libclpkm.so <command-to-run-ocl-app>

The runtime connects to user bus by default. You can make it connect to the system bus by passing `CLPKM_BUS_TYPE=system` along with other environment variables.

Benchmark
====================
TBD.

Known Issue
====================
TBD.

Future Work
====================
TBD.
