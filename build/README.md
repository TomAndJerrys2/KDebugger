# Building KDebugger

Building KDebugger is quite simple - this project taugh me, and I really
emphasize this, ALOT about cmake. Much more than what I was used to. With
this project each directory has its own unique stretch of Cmake goodies,
and with that, it allows for a more maintainable environment.

./test    - will be where the testing is done, specifically with Catch2
./build   - this directory, is where you are to build the project from source
./src     - This is where the bulk of the source files will live
./include - Before this, despite being pretty confident with C++23 and its 
            subsequent standards - I'd never used this. Seems like a Skill
            issue right? Well it definitely was lmao... With this directory
            public as well as our private headers all feature themselves in
            the project tree. While I'm not sure how this will affect binary
            size at the moment - this isn't too much of a concern. And if it
            is quite a lot bigger, I find this to be a good trade-off whilst
            I'm still developing this. the standard > 32Kb size will come later


# How to Install?
Like I said, this is your standard CMake'd project and a lot of it is trival
however, this will require some sort of bootstrapped VCPKG on your system

Don't worry too much about Libedit and Catch2 - these will be automatically
installed on build, so I and You shouldn't have to allocate mental memory for
such meanial tasks - the build goes as follows:

- cd build (assuming you're in the root dir of this project)
- cmake .. 
- make

and That's it - if for whatever reasons you encounter errors to do with VCPKG
then you can try:

- cd build
- cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/your/vcpkg/scripts/buildsystems/vcpkg.cmake

In this case, you are required to have VCPKG (which I'm assuming this wont be the case)
while the dependcy manager should be package with the build you can find it at:

- git clone https://github.com/microsoft/vcpkg.git
- ./vcpkg/bootstrap-vcpkg.sh

Personally, I'd never used vcpkg up until this project - following a tutorial
after I'm through the bulk of the tutorial the real work begins. I have some cool
Ideas around multi-threaded architecture and coroutines (and mabye some 23 & 26 features)

However, these aren't necessities - and despite common opinion these aren't to "show off"
1-2 years ago my GitHub will have mainly been for recruiters, this is true. But in the last
year my love for programming only got stronger as time went by. Now these implementations
are purely for myself - just showing myself I know what I'm doing essentially

infact, all of my projects are for myself - I stopped focussing on just "building" and have
actually started seeking knowledge and engineering tools that - I, myself would use in everday
development. Hopefully by the end, the a long with some other tools I'm thinking of building:
profiler, dissassembler, Linker for C&C++, (my own C compiler - however I did already build one
but it lacks general semantics and is indeed, very, very slow)

Thanks for checking out this page and thanks for building (hopefully) my love for programming
is burning brighter everday and hence, with it - my ideas. Stay coding dudes
- 
