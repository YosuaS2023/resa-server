# open.mp C API

A single-header C API for [open.mp](https://www.open.mp/) (Open Multiplayer), providing a comprehensive interface to create open.mp components and interact with the server.

## Overview

This library provides a complete C API for open.mp server functionality, allowing you to create components in C or any language that supports C FFI (Foreign Function Interface). The API covers all major aspects of the open.mp server including:

- **Players**: Player management, properties, actions, and state
- **Vehicles**: Vehicle creation, control, and modification
- **Objects**: Static and player-attached objects
- **Actors**: NPC actors with animations and properties
- **Checkpoints**: Race checkpoints and regular checkpoints
- **TextDraws**: 2D text drawing for HUD elements
- **TextLabels**: 3D text labels in the game world
- **Pickups**: Collectible items in the world
- **GangZones**: Territory marking and management
- **Menus**: Interactive menu systems
- **Dialogs**: Player dialog interfaces
- **Classes**: Player class/spawn configurations
- **Custom Models**: Custom model management
- **Events**: Event system for hooking into server events
- **Configuration**: Server configuration access
- **Core**: Core server functionality and utilities

## Features

- **Header-only**: Single header file with complete API definitions
- **Cross-platform**: Supports Windows and Unix-like systems (Linux, macOS, etc.)
- **FFI-friendly**: Can be used from any language supporting C FFI (Python, Rust, Go, etc.)
- **Complete API coverage**: Provides access to all open.mp server functionality
- **Dynamic loading**: Built-in support for dynamic library loading on both Windows and Unix

## Usage

### Basic Setup

1. Include the header file in your C/C++ project:

```c
#include "ompcapi.h"
```

2. Initialize the API by creating an `OMPAPI_t` instance and calling `omp_initialize_capi`:  

**Note:** This example shows how you can create a component with open.mp C-API too. It's not a must, as long as your library is somehow loaded, calling `omp_initialize_capi` alone works fine.  

```c
#include <ompcapi.h>
#include <stdio.h>

struct OMPAPI_t api;

void onReadyCallback() {
    api.Core.Log("Example - Ready callback");

    // Now you can use the API
    // Example: Create an actor
    int actor_id;
    void* actor = api.Actor.Create(123, 0.0f, 0.0f, 3.0f, 0.0f, &actor_id);

    // The CAPI doesn't support passing vargs, so you need to format it
    // before calling the Log function from the API
    char actor_msg[512];

    sprintf(actor_msg, "Created actor with ID: %i (at address 0x%x)",  actor_id, actor);
    api.Core.Log(actor_msg);
}

void onResetCallback() {
    api.Core.Log("Example - Reset callback");
}

void onFreeCallback() {
    api.Core.Log("Example - Free callback");
}

void ComponentEntryPoint() {
    if (!omp_initialize_capi(&api))
    {
        // Initialization failed - library not found or functions couldn't be loaded
        printf("Failed to initialize open.mp C API\n");
        return;
    }

    // Link the IComponent basic informations
    // UID, Component Name, Version, Callbacks
    // To generate a new UID you can use https://open.mp/uid
    return api.Component.Create(0x17C581AEC8711DD9, "Example", (struct ComponentVersion){
        1, 0, 0, 0
    }, onReadyCallback, onResetCallback, onFreeCallback);
}
```

The `omp_initialize_capi` function:
- Returns `true` if initialization succeeds
- Returns `false` if the library cannot be loaded or if API functions cannot be found
- Automatically loads the appropriate library (`$CAPI.dll` on Windows, `$CAPI.so` on Unix)
- Populates the `OMPAPI_t` structure with all available API functions

### Using with CMake

Add this repository as a subdirectory:

```cmake
add_subdirectory(open.mp-capi)
target_link_libraries(your_component PRIVATE OMP-CAPI)
```

## API Documentation

The complete API documentation is available in the `apidocs/` directory:
- `api.json`: Complete JSON specification of all API functions
- `events.json`: Event system documentation

## Project Structure

```
open.mp-capi/
├── include/
│   └── ompcapi.h                 # Main header file with complete API
├── apidocs/
│   ├── api.json                  # API function specifications
│   └── events.json               # Event system specifications
├── tools/
│   ├── generate_docs.js          # Generate the api.json file
│   └── generate_single_header.js # Generate the ompcapi.h if you cloned the main open.mp repository
├── CMakeLists.txt                # CMake configuration
├── LICENSE.md                    # Mozilla Public License 2.0
└── README.md                     # This file
```

## License

This project is licensed under the Mozilla Public License Version 2.0. See [LICENSE.md](LICENSE.md) for details.
