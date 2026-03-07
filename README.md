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
A Linux Debugger, Profiler and Disassembler built with C++ for 
extendable testing and feature-rich debugging; As it stands currently;
this project only features a LLDB/GDB inspired debugger would use. Cnce complete,
I'll move on to including a profiler:

    -  unit-testing automation, 
    -  disssssembler, 
    -  flamegraph support 
    - and... Possibly a unique, new build system

This was something I have wanted to do for a while - and all the better
with my love for C++. This project urrently uses C++17 however
I'm looking to (after completion) incorperate newer C++20 and 23 features
like coroutines etc... that may help utilize a more modern, asynchronous
architecture.
<br>
This project features a debugger tool that I personally use on my Linux
Fedora build for development. Following this, there is still a lot of room
for improvement... and a lot of other new features I am looking to add! Once I
start building out the multi-threading architecture for process' it should
be more apparent why this project, would be a great choice to put in your
toolchain. Experiment and have fun! all the feedback is appreciated!
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
KDebugger/
├── .gitignore
├── build/
│   ├── Makefile
│   └── README.md
├── CMakeLists.txt
├── include/
│   └── libkdebugger/
│       ├── bit.hpp
│       ├── breakpoint_site.hpp
│       ├── detail/
│       │   └── registers.inc
│       ├── disassembler.hpp
│       ├── error.hpp
│       ├── libkdebugger.hpp
│       ├── parse.hpp
│       ├── pipe.hpp
│       ├── process.hpp
│       ├── register_info.hpp
│       ├── registers.hpp
│       ├── stoppoint_collection.hpp
│       ├── types.hpp
│       └── watchpoint.hpp
├── LICENSE
├── README.md
├── src/
│   ├── breakpoint_site.cpp
│   ├── CMakeLists.txt
│   ├── disassembler.cpp
│   ├── libkdebugger.cpp
│   ├── pipe.cpp
│   ├── process.cpp
│   ├── registers.cpp
│   └── watchpoint.cpp
├── test/
│   ├── CMakeLists.txt
│   ├── targets/
│   │   ├── anti_debugger.cpp
│   │   ├── CMakeLists.txt
│   │   ├── end_immediately.cpp
│   │   ├── hello_kdebugger.cpp
│   │   ├── memory.cpp
│   │   ├── reg_read.s
│   │   ├── reg_write.s
│   │   └── run_endlessly.cpp
│   └── tests.cpp
├── tools/
│   ├── CMakeLists.txt
│   └── kdebugger.cpp
└── vcpkg.json
```

# Scope
> [!NOTE]
> Features with ⚠️ are either in-development or have yet to be added
1. A Basice Debugger Capable of interacting with Process ✅
2. Provides User with Memory Address Locations and single-stepping ✅
3. Disassembling Code to x64 ASM and allows stepping and memory modification ✅
4. Software and Hardware Breakpoint functionality ✅
5. Extended Watchpoint and Catchpoint functionality around Systemcalls ✅
6. Signal and Error handling based on unit testing (via Catch2) ✅
7. Multi-Threaded Architecture ⚠️
8. In-Depth Debug Information and Improvement statistics ⚠️
9. Line Tables and Source-Level breakpoints ⚠️
10. Stack Frame & Unwinding unique view ⚠️
11. Internal library observation ⚠️
12. Linking and Encoding information ⚠️
13. Profiler and Code-Generation Optimization Techniques ⚠️
14. Custom Disassembler and Profiler Integration (Big Goal) ⚠️
15. Flame Graph Support and Logging ⚠️

# Features
KDebugger> help
- memory
- disassemble
- breakpoint
- watchpoint
- catchpoint
- step
- continue
- more to come...

# Build
This project takes advantage of CMake 3.29 and is used in accompany with VCPKG
Catch2 and Libedit. Catch2 and Libedit are dependencies you'll need to install
via your package manager. VCPKG takes care of including these dependencies in our
project tree structure, to build:

```
cd build && cmake ..
```

In case VCPKG, encounters errors - boostrap it and run this in build.
```
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/your/vcpkg/scripts/buildsystems/vcpkg.cmake
```

```
make
```

## 📜 License

This project is licensed under the GPL License.
