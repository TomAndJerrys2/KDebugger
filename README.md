# KDebugger
![License](https://img.shields.io/badge/license-GPL-green)

> [!NOTE]
> This is KDebugger. A Linux Debugger and it is very much still under development
> once finished, I should have some more time to create the surrounding tool chain
> of the "K" tools :)
<br>

> [!IMPORTANT]
> This is a strictly Linux Based Debugger (for now) and complies with
> the POSIX standards. If you're not running linux and intend to test
> this on windows - I will eventually come to make a port :)
<br>

# Foreword
A Linux debugger (and eventually a profiler) built with C++ for 
extendable testing and feature-rich debugging. As it stand currently
This project only features a LLDB/GDB inspired debugger, once complete
I'll move on to including a profiler, unit-testing automation, 
disssssembler, flamegraph support and possible a unique, new build system

This was something I have wanted to do for a while and all the better
with my love for C++. This project will and currently uses C++17 however
I'm looking too (after completion) to incorperate newer C++20 and 23 features
like coroutines etc... that may help utilize a more modern, asynchronous
architecture.
<br>
This project features a debugger tool that I personally use on my Linux
Fedora build for development. Following this I'll make my own dissasembler
and other tools and eventually - fork them into a stuite of tools
<br>
# Why
I've always wanted to built a debugger and the tools surrounding it.
This along with my eventual, dissassembler and profiler will hopefully
make its way into a suite of tools I can use personally.

Really, this is just about getting better with C++ - I aim to improve 
and learn new things everyday and granted, this is definitely the project
for such a task. At the moment, It will remain in C++17, when I find time
to convert functions and make them asynchronously and add coroutines, mabye
some concepts and meta-template programming - these will be in the separate 
given branches for people to see how these conversions work
<br>
# Project File Structure
```
.
├── CMakeLists.txt
├── LICENSE
├── include
│   └── libkdebugger
│       ├── error.hpp
│       ├── libkdebugger.hpp
│       ├── pipe.hpp
│       └── process.hpp
├── src
│   ├── CMakeLists.txt
│   ├── libkdebugger.cpp
│   ├── pipe.cpp
│   └── process.cpp
├── test
│   ├── CMakeLists.txt
│   └── tests.cpp
├── tools
│   ├── CMakeLists.txt
│   └── kdebugger.cpp
└── vcpkg.json
```
# Build

This project takes advantage of CMake 3.29 and is used in accompany with VCPKG
Catch2 and Libedit. Catch2 and Libedit are dependencies you'll need to install
via your package manager. VCPKG takes care of including these dependencies in our
project tree structure, to build:

```
cd build && cmake ..
```
```
make
```

## 📜 License

This project is licensed under the GPL License.
