# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenTTD is a transport simulation game (C++20). This fork adds **AI agent integration** via JSON-RPC server (port 9877) and a CLI tool (`ttdctl`), enabling Claude Code to play the game through an embedded terminal window.

## Build Commands

```bash
# Standard build (Debug by default)
mkdir build && cd build
cmake ..
make -j$(nproc)

# Release build (significantly faster runtime)
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc)

# macOS with Ninja (faster builds)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja

# Run tests
ctest --output-on-failure

# Build only ttdctl CLI tool
cmake --build . --target ttdctl

# Dedicated server (no GUI)
cmake .. -DOPTION_DEDICATED=ON
```

## Key Directories

```
src/
├── rpc/                    # JSON-RPC server for AI agents (port 9877)
│   ├── rpc_handlers_query.cpp    # Read-only state queries
│   ├── rpc_handlers_action.cpp   # Vehicle/order mutations
│   └── rpc_handlers_infra.cpp    # Building infrastructure
├── terminal/               # Embedded terminal (libvterm + PTY)
├── script/api/             # 76 header files - game logic API reference
├── *_cmd.cpp               # Command pattern for all game mutations
└── tests/                  # Catch2 unit tests

ttdctl/                     # CLI tool for RPC communication
├── src/
│   ├── commands_query.cpp
│   ├── commands_action.cpp
│   └── commands_infra.cpp

ai-agent-workspace/         # Agent instruction files
├── OVERSEER.md             # Strategic coordinator agent
├── TRAIN_SPECIALIST.md     # Rail network specialist
├── ROAD_SPECIALIST.md      # Road vehicle specialist
├── MARINE_SPECIALIST.md    # Ship specialist
└── AIR_SPECIALIST.md       # Aircraft specialist
```

## Architecture Patterns

**Command Pattern**: All game mutations go through `*_cmd.cpp` files and return `CommandCost` (enables undo, validation, network sync). Never modify game state directly.

**RPC Handler Structure**: Handlers in `src/rpc/rpc_handlers_*.cpp` follow this pattern:
```cpp
static nlohmann::json HandleXxx(const nlohmann::json &params) {
    // Validate params
    // Call game APIs
    // Return JSON response
}
// Register at bottom of file:
RpcMethodRegistry::Register("category.action", HandleXxx);
```

**Script API Reference**: When implementing RPC handlers, consult `src/script/api/script_*.hpp` for existing game logic wrappers. Example: `ScriptVehicle::GetState()`, `ScriptOrder::AppendOrder()`.

**nlohmann/json Initialization**: Always use `nlohmann::json::object()` not `nlohmann::json params;` (the latter creates null, causing `.value()` calls to throw).

## ttdctl CLI

The CLI tool communicates with the game via JSON-RPC:

```bash
# Examples
./build/ttdctl/ttdctl ping
./build/ttdctl/ttdctl vehicle list train
./build/ttdctl/ttdctl engine list --type road
./build/ttdctl/ttdctl order append <vehicle_id> --station <station_id>
./build/ttdctl/ttdctl rail track <x1> <y1> <x2> <y2>
```

Add new commands by:
1. Adding handler in `ttdctl/src/commands_*.cpp`
2. Declaring in `ttdctl/src/cli_common.h`
3. Adding routing in `ttdctl/src/main.cpp`

## Adding RPC Handlers

1. Add handler function in appropriate `src/rpc/rpc_handlers_*.cpp`
2. Register with `RpcMethodRegistry::Register("category.action", HandlerFunc)`
3. Game must be restarted to load new handlers
4. Add corresponding ttdctl command for testing

## Testing

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific test
./build/openttd_test --reporter compact --name "TestName"
```

Mock objects available: `mock_environment.h`, `mock_fontcache.h`, `mock_spritecache.h`.

## Code Style

See `CODINGSTYLE.md` for full details. Key points:
- Tabs for indentation
- CamelCase for functions/classes, lowercase_with_underscores for variables
- Commit format: `<keyword> [#issue] [component]: Description`
- Keywords: Add, Fix, Change, Remove, Doc, Cleanup

## AI Agent System

The game includes a multi-agent system where Claude Code instances can collaboratively run a transport company. See `AGENT_INTEGRATION_PLAN.md` for full architecture. The embedded terminal (`src/terminal/`) spawns Claude Code with `ttdctl` in PATH and working directory set to `ai-agent-workspace/`.
