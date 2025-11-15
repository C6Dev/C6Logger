# C6Logger

![Copy of C svg](https://github.com/user-attachments/assets/de9be743-1f0b-4ab7-b707-cb958125cc90)

C6Logger is a lightweight, user-friendly logging library designed for resource efficiency and easy integration.

## Features

- Simple API for logging messages at various levels
- Efficient resource usage
- Easy integration with CMake
- MIT License

## Getting Started

### 1. Build the Library

C6Logger provides a `CMakeLists.txt` to build a `.lib` file. Add C6Logger to your project and build the library using CMake:

```sh
cmake .
cmake --build .
```

### 2. Include the Header

In your code, include the logger header:

```cpp
#include "Logger.h"
```

### 3. Usage Example

All logger functionality is under the `C6Logger` namespace. To log a message:

```cpp
C6Logger::Log(C6Logger::LogLevel::Info, "Your message", "Messenger");
```

- The `Messenger` parameter is optional.

## License

C6Logger is released under the MIT License. You are free to use, modify, and distribute it as you wish.
