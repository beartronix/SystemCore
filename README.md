
<h2 id="processing-start" style="display:none;"></h2>

![GitHub](https://img.shields.io/github/license/NoOrientationProgramming/SystemCore?style=plastic&color=blue)
![GitHub Release](https://img.shields.io/github/v/release/NoOrientationProgramming/SystemCore?style=plastic&color=blue)
[![Standard](https://img.shields.io/badge/standard-C%2B%2B11-blue.svg?style=plastic&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)

![Windows](https://img.shields.io/github/actions/workflow/status/NoOrientationProgramming/code-orb/windows.yml?style=plastic&logo=github&label=Windows)
![Linux](https://img.shields.io/github/actions/workflow/status/NoOrientationProgramming/code-orb/linux.yml?style=plastic&logo=linux&logoColor=white&label=Linux)
![MacOS](https://img.shields.io/github/actions/workflow/status/NoOrientationProgramming/code-orb/macos.yml?style=plastic&logo=apple&label=MacOS)
![FreeBSD](https://img.shields.io/github/actions/workflow/status/NoOrientationProgramming/code-orb/freebsd.yml?style=plastic&logo=freebsd&label=FreeBSD)
![ARM, RISC-V & MinGW](https://img.shields.io/github/actions/workflow/status/NoOrientationProgramming/code-orb/cross.yml?style=plastic&logo=gnu&label=ARM%2C%20RISC-V%20%26%20MinGW)

[![Discord](https://img.shields.io/discord/960639692213190719?style=plastic&color=purple&logo=discord)](https://discord.gg/FBVKJTaY)

This C++ framework provides a lightweight and highly structured foundation for C++ applications, combining a minimalistic design with a powerful debugging and monitoring system.

It allows you to focus entirely on your application's logic without being restricted in your coding style. Similar to LaTeX, it separates structure from content - enabling faster, more reliable development, even for complex systems.

## Advantages

- **Minimal boilerplate** > Focus purely on task-specific logic
- **Recursive process structure** > Uniform system architecture from simple to complex
- **Integrated debugging** > Visual process tree, logs and command interface over TCP
- **Memory-safe design** > Aligned lifetimes of processes and data, reducing leaks
- **Cross-platform** > Windows, Linux, MacOS, FreeBSD, uC (STM32, ESP32, ARM, RISC-V)

## Requirements

- C++ standard as low as C++11 can be used
- On Microcontrollers: Minimum of 32k flash memory

### How to add to your project

`git submodule add https://github.com/NoOrientationProgramming/SystemCore.git`

### Use Templates!

To implement a new process you can use the provided shell scripts on linux: [cppprocessing.sh](https://github.com/NoOrientationProgramming/SystemCore/blob/main/tools/cppprocessing.sh) / [cppprocessing_simple.sh](https://github.com/NoOrientationProgramming/SystemCore/blob/main/tools/cppprocessing_simple.sh)

Or just create your own..

## Learn how to use it

The [Tutorials](https://github.com/NoOrientationProgramming/NopTutorials) provide more information on how to delve into this wonderful (recursive) world ..
