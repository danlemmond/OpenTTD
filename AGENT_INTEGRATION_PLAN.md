# OpenTTD AI Agent Integration Plan

## Overview

This document outlines the plan to integrate a Claude Code AI agent terminal into OpenTTD, similar to the RampLabs experiment done with OpenRCT2.

## Status: Phase 3 Complete - Full RPC API + Infrastructure + Economic Analytics

Started: 2025-12-25
Last Updated: 2025-12-25

---

## Architecture Goal

```
┌─────────────────────────────────────────────────────────────┐
│                      OpenTTD (Fork)                         │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ AI Agent Terminal Window (libvterm + PTY)             │  │
│  │  • Spawns Claude Code automatically                   │  │
│  │  • ANSI color support                                 │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ spawns                              │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ Claude Code Process                                   │  │
│  │  • Working dir: ai-agent-workspace/                   │  │
│  │  • ttdctl in PATH                                     │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ invokes                             │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ JSON-RPC Server (localhost:9877)                      │  │
│  │  • Exposes game state and actions                     │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ calls                               │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ OpenTTD Game APIs                                     │  │
│  │  • Vehicles, Stations, Orders, Industries, Towns...   │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘

External Tool:
┌──────────────┐      ┌──────────────┐
│   ttdctl     │─────▶│  JSON-RPC    │
│  (CLI tool)  │      │  (port 9877) │
└──────────────┘      └──────────────┘
```

---

## Exploration Notes

### 1. Existing Script API (src/script/api/)

**76 API header files** - This is a goldmine! The existing AI/GameScript API covers:

| Category | Classes | Purpose |
|----------|---------|---------|
| Vehicles | `ScriptVehicle`, `ScriptVehicleList` | Query/build all vehicle types |
| Stations | `ScriptStation`, `ScriptStationList` | Train stations, bus stops, airports, docks |
| Orders | `ScriptOrder` | Route management with complex flags |
| Industries | `ScriptIndustry`, `ScriptIndustryType` | Production chains |
| Towns | `ScriptTown`, `ScriptTownList` | Town growth, ratings |
| Infrastructure | `ScriptRail`, `ScriptRoad`, `ScriptMarine` | Track/road building |
| Cargo | `ScriptCargo`, `ScriptCargoList` | Cargo types and monitoring |
| Company | `ScriptCompany` | Finances, company management |
| Map | `ScriptMap`, `ScriptTile` | Terrain, tile queries |
| Groups | `ScriptGroup` | Vehicle grouping |
| Signs | `ScriptSign` | Map annotations |

**Key Insight:** These classes define exactly what our RPC handlers need to expose. We can either:
1. Call the static methods directly from RPC handlers
2. Reuse the underlying game logic they wrap

The `ScriptOrder` class is particularly rich - it handles the complexity of route management with flags like `OF_FULL_LOAD`, `OF_TRANSFER`, conditional orders, etc.

### 2. Console System (src/console*.cpp)

OpenTTD has a mature in-game console with:
- Command registration via `IConsoleCmdRegister()`
- Hook system for permission checks (`ConHookServerOnly`, `ConHookNoNetwork`, etc.)
- Help text system (commands return help when called with empty args)
- File list caching for save/load operations

**Console Commands Pattern:**
```cpp
static bool ConZoomToLevel(std::span<std::string_view> argv)
{
    if (argv.empty()) {
        IConsolePrint(CC_HELP, "Usage: 'zoomto <level>'.");
        return true;
    }
    // ... implementation
}
```

**Relevance:** We could potentially add new console commands as an alternative to JSON-RPC, but JSON-RPC is better for structured data and AI consumption.

### 3. Admin Network Interface (src/network/network_admin.*)

**This is a hidden gem!** OpenTTD already has a remote administration protocol:

| Feature | Method | Description |
|---------|--------|-------------|
| RCON | `Receive_ADMIN_RCON` | Execute console commands remotely |
| Console | `SendConsole` | Receive console output |
| GameScript | `SendGameScript` | JSON communication with scripts |
| Company Info | `SendCompanyInfo/Update` | Company statistics |
| Client Management | `SendClientJoin/Quit` | Player tracking |
| Chat | `SendChat` | In-game messaging |

**Key Methods:**
- `NetworkAdminGameScript(std::string_view json)` - Already has JSON communication!
- `NetworkServerSendAdminRcon()` - Remote command execution

**Design Decision:** We could either:
1. Build JSON-RPC as a new server (like OpenRCT2)
2. Extend the admin protocol with structured RPC
3. Use admin protocol + RCON for simpler integration

Recommendation: **New JSON-RPC server** for consistency with OpenRCT2 approach and cleaner API design.

### 4. Window System

OpenTTD has a well-structured window system in `src/window*.cpp`:

**Console GUI as Reference** (`console_gui.cpp`):
- `IConsoleWindow` struct inherits from `Window`
- Uses `Textbuf` for input handling
- Has autocomplete support
- Line-based buffer with colors
- History navigation

**Window Registration Pattern:**
```cpp
static constexpr std::initializer_list<NWidgetPart> _nested_console_window_widgets = {
    NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_BACKGROUND), SetResize(1, 1),
};

static WindowDesc _console_window_desc(
    WDP_MANUAL, {}, 0, 0,
    WC_CONSOLE, WC_NONE,
    {},
    _nested_console_window_widgets
);
```

**For Terminal Window:** We'd add a new `WC_AI_AGENT_TERMINAL` window class and create a similar window struct with libvterm integration.

### 5. Command/Action System

OpenTTD uses a `CommandCost` pattern for all game mutations:

**Command Files:** `*_cmd.cpp` (20+ files)
- `rail_cmd.cpp` - Track building
- `station_cmd.cpp` - Station construction
- `order_cmd.cpp` - Route management
- `vehicle_cmd.cpp` - Vehicle operations

**CommandCost Pattern:**
```cpp
class CommandCost {
    Money cost;
    StringID message;
    ExpensesType expense_type;
    bool success;
    // ...
};
```

This is analogous to OpenRCT2's Game Actions - all mutations return cost/success, enabling:
- Undo support
- Validation before execution
- Network sync

### 6. Domain Complexity Analysis

OpenTTD is significantly more complex than OpenRCT2 in terms of game mechanics:

#### Vehicle Types (4 vs 1 in RCT)
| Type | Subtypes | Complexity |
|------|----------|------------|
| Rail | Trains, wagons, multiple unit composition | Signals, track types, electrification |
| Road | Buses, trucks, trams | Road types, one-way roads |
| Water | Ships, ferries | Canals, locks, buoys |
| Air | Planes, helicopters | Airports, runways, hangars |

#### Cargo System
- **27+ cargo types** across climates (passengers, coal, mail, oil, livestock, goods, etc.)
- **Production chains**: Coal Mine → Power Plant, Forest → Sawmill → Furniture Factory
- **Cargo rating**: Station rating affects what industries send to you
- **Transfer/feeder systems**: Complex multi-hop routes

#### Infrastructure
- **Track building**: Unlike RCT rides, you build individual track pieces
- **Signals**: Block, path, entry/exit, combo - critical for train networks
- **Junctions**: Complex intersections require planning
- **Bridges/tunnels**: Height considerations

#### Orders System (Most Complex Part)
```
ScriptOrder flags:
- OF_NON_STOP_INTERMEDIATE/DESTINATION
- OF_UNLOAD / OF_TRANSFER / OF_NO_UNLOAD
- OF_FULL_LOAD / OF_FULL_LOAD_ANY / OF_NO_LOAD
- OF_SERVICE_IF_NEEDED / OF_STOP_IN_DEPOT
- Conditional orders (if load% > X, goto order Y)
- Timetables
```

#### Industries
- Production levels (0x00-0x80)
- Stockpiling for processing industries
- Closure mechanics
- Town/industry relationships

#### Towns
- Town growth based on service
- Local authority rating
- Town actions (advertising, bribes, etc.)

**Estimate:** ~3x more RPC handlers than OpenRCT2, but the existing Script API provides excellent documentation.

---

## Implementation Plan

### Phase 1: Foundation (JSON-RPC Server)

**Goal:** Basic RPC communication without terminal UI

**Files to Create:**
```
src/
├── scripting/                      # New directory (or extend existing script/)
│   ├── JsonRpcServer.cpp          # TCP server on localhost:9877
│   ├── JsonRpcServer.h
│   └── rpc/
│       ├── HandlerRegistry.cpp    # Method dispatch
│       ├── HandlerRegistry.h
│       ├── RpcTypes.h             # Common types
│       └── handlers/
│           ├── CompanyHandlers.cpp
│           ├── VehicleHandlers.cpp
│           └── ...
```

**Key Decision:** Port 9877 (9876 is used by OpenRCT2)

**Dependencies:** nlohmann/json ✅ Already in `src/3rdparty/nlohmann/json.hpp` and used by crashlog, genworld, survey code

### Phase 2: Core RPC Handlers

**Priority Order (based on gameplay importance):**

1. **Company/Finance** - Cash, loans, company info
2. **Map/Tile** - Terrain queries, tile info
3. **Vehicle** - List, status, buy, sell, start/stop
4. **Station** - List, cargo waiting, ratings
5. **Order** - Route management (complex!)
6. **Industry** - Production, cargo chains
7. **Town** - Growth, ratings, actions
8. **Infrastructure** - Rail/road/water building (hardest)

**Approach:** Start with read-only handlers, then add mutations

### Phase 3: CLI Tool (ttdctl)

**Structure:**
```
ttdctl/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── cli/
│   ├── commands/
│   ├── renderers/
│   └── rpc/
└── include/
```

**Command Examples:**
```bash
ttdctl company status
ttdctl vehicles list --type=train
ttdctl vehicle 42 orders
ttdctl station 5 cargo
ttdctl industry list --produces=coal
ttdctl town 3 rating
ttdctl map tile 100 200
```

### Phase 4: Terminal Window

**Components to Port from OpenRCT2:**
- `ShellProcess.{h,cpp}` - PTY abstraction
- `TerminalSession.{h,cpp}` - libvterm wrapper
- `AIAgentLaunch.{h,cpp}` - Claude spawning logic

**New Window:**
- Add `WC_AI_AGENT_TERMINAL` to window_type.h
- Create `ai_agent_terminal_gui.cpp`
- Add toolbar button

**Dependencies:** libvterm

### Phase 5: Session Management

- Session file monitoring for turn detection
- Auto-prompt rotation
- Session logging

---

## File Structure (Proposed)

```
OpenTTD/
├── src/
│   ├── rpc/                           # NEW: JSON-RPC infrastructure
│   │   ├── JsonRpcServer.cpp
│   │   ├── JsonRpcServer.h
│   │   ├── HandlerRegistry.cpp
│   │   ├── HandlerRegistry.h
│   │   ├── RpcTypes.h
│   │   └── handlers/
│   │       ├── CompanyHandlers.cpp
│   │       ├── VehicleHandlers.cpp
│   │       ├── StationHandlers.cpp
│   │       ├── OrderHandlers.cpp
│   │       ├── IndustryHandlers.cpp
│   │       ├── TownHandlers.cpp
│   │       ├── MapHandlers.cpp
│   │       └── InfrastructureHandlers.cpp
│   ├── terminal/                      # NEW: Terminal infrastructure
│   │   ├── ShellProcess.cpp
│   │   ├── ShellProcess.h
│   │   ├── TerminalSession.cpp
│   │   ├── TerminalSession.h
│   │   ├── AIAgentLaunch.cpp
│   │   └── AIAgentLaunch.h
│   └── ai_agent_terminal_gui.cpp      # NEW: Terminal window
├── ttdctl/                            # NEW: CLI tool
│   ├── CMakeLists.txt
│   ├── src/
│   └── include/
├── ai-agent-workspace/                # NEW: Claude's working directory
│   └── IN_GAME_AGENT.md
└── AGENT_INTEGRATION_PLAN.md          # This file
```

---

## Key Differences from OpenRCT2 Approach

| Aspect | OpenRCT2 | OpenTTD |
|--------|----------|---------|
| Script API | Duktape (JS) | Squirrel |
| Existing Admin Port | No | Yes (could leverage) |
| Console | Limited | Full featured |
| Domain Complexity | Rides + Guests | Vehicles + Cargo + Routes |
| Map Size | Small parks | Up to 4096×4096 |
| Infrastructure | Prefab rides | Individual track pieces |

---

## Open Questions

1. **Should we leverage the existing admin port?**
   - Pro: Less code to write
   - Con: Different protocol, may limit flexibility

2. **How to handle track building?**
   - Individual tiles vs. path-finding assisted placement
   - Signal placement strategy

3. **Cargo chain visualization?**
   - Text-based industry chain diagrams
   - Profitability analysis per route

4. **Map representation for CLI?**
   - ASCII art for small areas?
   - Coordinate-based queries?

---

## Detailed RPC Handler Specification

Based on analysis of the Script API, here are the handlers needed:

### Company Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `company.list` | ✅ | List all companies | `ScriptCompanyList` |
| `company.setLoan` | ✅ | Adjust loan amount | `ScriptCompany::SetLoanAmount` |

### Vehicle Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `vehicle.list` | ✅ | List all vehicles | `ScriptVehicleList` |
| `vehicle.get` | ✅ | Get vehicle details | `ScriptVehicle` |
| `vehicle.getCargoByType` | ✅ | Cargo breakdown per type | Vehicle cargo iteration |
| `vehicle.build` | ✅ | Purchase new vehicle | `ScriptVehicle::BuildVehicle` |
| `vehicle.sell` | ✅ | Sell vehicle | `ScriptVehicle::SellVehicle` |
| `vehicle.clone` | ✅ | Clone vehicle with orders | `ScriptVehicle::CloneVehicle` |
| `vehicle.startstop` | ✅ | Toggle start/stop | `ScriptVehicle::StartStopVehicle` |
| `vehicle.depot` | ✅ | Send to depot | `ScriptVehicle::SendVehicleToDepot` |
| `vehicle.turnaround` | ✅ | Cancel depot order | (custom) |
| `vehicle.refit` | ✅ | Change cargo type | `ScriptVehicle::RefitVehicle` |

### Engine Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `engine.list` | ✅ | List available engines | `ScriptEngineList` |
| `engine.get` | ✅ | Get engine details | `ScriptEngine` |

### Station Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `station.list` | ✅ | List all stations | `ScriptStationList` |
| `station.get` | ✅ | Get station details with cargo | `ScriptStation` |
| `station.getCargoPlanned` | ✅ | Cargo flow from link graph | LinkGraph edges |

### Order Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `order.list` | ✅ | Get vehicle orders | `ScriptOrder::GetOrderCount` |
| `order.append` | ✅ | Add order to end | `ScriptOrder::AppendOrder` |
| `order.remove` | ✅ | Remove order | `ScriptOrder::RemoveOrder` |
| `order.insert` | ✅ | Insert order at position | `ScriptOrder::InsertOrder` |
| `order.setFlags` | ✅ | Set load/unload flags | `ScriptOrder::SetOrderFlags` |
| `order.share` | ✅ | Share orders between vehicles | `ScriptOrder::ShareOrders` |

### Industry Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `industry.list` | ✅ | List all industries | `ScriptIndustryList` |
| `industry.get` | ✅ | Get industry details with production | `ScriptIndustry` |
| `industry.getStockpile` | ✅ | Cargo stockpiled at industry | `ScriptIndustry::GetStockpiledCargo` |
| `industry.getAcceptance` | ✅ | What cargo an industry accepts | `ScriptIndustryType::GetAcceptedCargo` |

### Town Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `town.list` | ✅ | List all towns | `ScriptTownList` |
| `town.get` | ✅ | Get town details with ratings | `ScriptTown` |
| `town.performAction` | ✅ | Town actions (ads, etc) | `ScriptTown::PerformTownAction` |

### Map/Tile Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `map.info` | ✅ | Get map dimensions | `ScriptMap::GetMapSize` |
| `map.distance` | ✅ | Manhattan distance | `ScriptMap::DistanceManhattan` |
| `map.scan` | ✅ | ASCII map visualization | (custom) |
| `tile.get` | ✅ | Get tile info | `ScriptTile` |
| `tile.getRoadInfo` | ✅ | Road orientation info | (custom) |

### Infrastructure Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `road.build` | ✅ | Build road | `ScriptRoad::BuildRoad` |
| `road.buildStop` | ✅ | Build bus/truck stop | `ScriptRoad::BuildRoadStation` |
| `road.buildDepot` | ✅ | Build road depot | `ScriptRoad::BuildRoadDepot` |
| `rail.buildTrack` | ✅ | Build rail track | `ScriptRail::BuildRailTrack` |
| `rail.buildStation` | ✅ | Build train station | `ScriptRail::BuildRailStation` |
| `rail.buildDepot` | ✅ | Build train depot | `ScriptRail::BuildRailDepot` |
| `rail.buildSignal` | ✅ | Build signal (all types) | `CMD_BUILD_SINGLE_SIGNAL` |
| `rail.removeSignal` | ✅ | Remove signal | `CMD_REMOVE_SINGLE_SIGNAL` |
| `marine.buildDock` | ✅ | Build dock | `CMD_BUILD_DOCK` |
| `marine.buildDepot` | ✅ | Build ship depot | `CMD_BUILD_SHIP_DEPOT` |
| `airport.build` | ✅ | Build airport (all types) | `CMD_BUILD_AIRPORT` |

### Economic/Analytics Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `subsidy.list` | ✅ | Available subsidy opportunities | `ScriptSubsidy` iteration |
| `cargo.list` | ✅ | All cargo types with properties | `ScriptCargo` iteration |
| `cargo.getIncome` | ✅ | Calculate income for cargo route | `GetTransportedGoodsIncome` |
| `cargomonitor.getDelivery` | ✅ | Cargo delivered to industry/town | `CargoMonitor::GetDeliveryAmount` |
| `cargomonitor.getPickup` | ✅ | Cargo picked up from industry/town | `CargoMonitor::GetPickupAmount` |
| `airport.info` | ✅ | Airport type specifications | `AirportSpec` |

#### Infrastructure Placement Notes
When searching for valid build locations, these sizes and anchor points apply:

| Structure | Size (WxH) | Anchor | Notes |
|-----------|------------|--------|-------|
| Small airport | 3x4 | Top-left | All tiles must be flat, same height |
| Large airport | 4x5 | Top-left | |
| Metropolitan | 5x5 | Top-left | |
| International | 6x6 | Top-left | |
| Intercontinental | 9x11 | Top-left | |
| Commuter | 4x4 | Top-left | |
| Heliport | 1x1 | Single tile | Available from 1963 |
| Helidepot | 2x2 | Top-left | |
| Helistation | 2x3 | Top-left | |
| Ship depot | 1x2 | Left tile | Axis determines orientation (x=horizontal, y=vertical) |
| Dock | 1x2 | Land tile | Requires sloped coastal tile adjacent to water |
| Rail station | PxL | Top-left | P=platforms (width), L=length (height) |

To find valid airport locations, scan tile clusters checking that all tiles in the footprint are:
1. Flat (same height at all corners)
2. Same height as each other
3. Clear land (not water, not owned by others)

### Meta Handlers
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `ping` | ✅ | Health check | (custom) |
| `game.status` | ✅ | Game date/status | (custom) |
| `game.newgame` | ✅ | Start new game | `StartNewGameWithoutGUI` |

### Viewport/Camera Handlers (for streaming)
| Method | Status | Description | Script API Reference |
|--------|--------|-------------|---------------------|
| `viewport.goto` | ✅ | Scroll viewport to tile | (custom) |
| `viewport.follow` | ✅ | Follow a vehicle | (custom) |
| `activity.hotspot` | ✅ | Get most active area | (custom) |
| `activity.clear` | ✅ | Clear activity history | (custom) |

---

## Agent Strategy Considerations

For Claude to effectively play OpenTTD, the CLI needs to support these gameplay patterns:

### Basic Transport Company Flow
1. **Survey the map** - Find industries, towns, terrain
2. **Identify profitable routes** - Coal mine → Power plant, Forest → Sawmill
3. **Build infrastructure** - Track/road between source and destination
4. **Build stations** - At source and destination
5. **Buy vehicles** - Appropriate for cargo type
6. **Set orders** - Load at source, unload at destination
7. **Monitor performance** - Profits, ratings, bottlenecks

### Key Information Claude Needs
```bash
# What industries exist and what do they produce/accept?
ttdctl industry list --verbose

# What cargo can be transported between two points?
ttdctl route suggest --from="Coal Mine #3" --to-type=power_plant

# What's the terrain like between two points?
ttdctl map path 100,50 200,75

# How is my route performing?
ttdctl station 5 stats
ttdctl vehicle 42 profit
```

### Challenges for AI
1. **Spatial reasoning** - Track placement, avoiding obstacles
2. **Network effects** - Signals, junctions, capacity planning
3. **Economic optimization** - Which routes are most profitable
4. **Long-term planning** - Town growth, industry closure

### Suggested CLI Helpers
```bash
# Higher-level commands that abstract complexity
ttdctl route create --from="Coal Mine #3" --to="Power Station #1" --type=rail
# Would: survey terrain, build track, add stations, buy trains, set orders

ttdctl line analyze 42
# Would: show profit/loss, bottlenecks, improvement suggestions
```

---

## Multi-Agent Architecture

### Overview

Rather than a single agent managing everything, we'll use a **coordinator + specialist** pattern:

```
┌─────────────────────────────────────────────────────────────┐
│                    Coordinator Agent                         │
│                      (Opus 4.5)                              │
│                                                              │
│  Responsibilities:                                           │
│  • Strategic planning (which routes to develop)              │
│  • Budget allocation (per transport type, per round)         │
│  • Conflict resolution (shared infrastructure)               │
│  • Performance monitoring (company-wide metrics)             │
│  • Round management (5-minute chunks)                        │
│  • Delegating tasks to specialists                           │
└────┬─────────────┬─────────────┬─────────────┬──────────────┘
     │             │             │             │
┌────▼────┐ ┌──────▼──────┐ ┌────▼────┐ ┌──────▼──────┐
│  Rail   │ │    Road     │ │   Sea   │ │    Air      │
│  Agent  │ │    Agent    │ │  Agent  │ │   Agent     │
│         │ │             │ │         │ │             │
│• Track  │ │• Bus routes │ │• Ships  │ │• Airports   │
│• Signals│ │• Trucking   │ │• Docks  │ │• Aircraft   │
│• Trains │ │• Trams      │ │• Canals │ │• Helicopters│
└─────────┘ └─────────────┘ └─────────┘ └─────────────┘
```

### Agent Instruction Files

```
ai-agent-workspace/
├── COORDINATOR.md          # Strategic oversight, round management, budget allocation
├── RAIL_SPECIALIST.md      # Train networks, signals, junctions
├── ROAD_SPECIALIST.md      # Buses, trucks, trams, road networks
├── SEA_SPECIALIST.md       # Ships, docks, canals, ferries
└── AIR_SPECIALIST.md       # Airports, aircraft, helicopters
```

### Why This Works

1. **Domain Complexity Isolation**
   - Rail is the most complex (signals, electrification, junctions)
   - Road is simpler but has different concerns (town coverage, trams)
   - Sea is niche but important for bulk cargo over water

2. **Parallel Development**
   - Rail agent builds coal network while Road agent sets up town buses
   - No blocking on single agent's turn

3. **Focused Context**
   - Rail agent doesn't need to know ship mechanics
   - Each specialist gets targeted instructions

4. **Natural Game Separation**
   - OpenTTD UI separates these (Rail toolbar, Road toolbar, etc.)
   - Different vehicle depots, different build tools

### Coordination Challenges

| Challenge | Solution |
|-----------|----------|
| **Shared budget** | Coordinator allocates budget per agent, agents report spending |
| **Geographic conflicts** | Coordinator assigns regions or arbitrates disputes |
| **Inter-modal transfers** | Coordinator plans transfer stations, specialists build their half |
| **Priority conflicts** | Coordinator sets priorities (e.g., "rail coal route first") |

### Communication Protocol

Specialists report to coordinator:
```
RAIL_AGENT: Completed coal line from Mine #3 to Power Plant #7
            Cost: $450,000 | Expected annual profit: $120,000
            Requesting 3 additional trains ($75,000 budget needed)

COORDINATOR: Approved. Road agent - defer town bus expansion,
             redirect $75,000 to rail. Rail agent - proceed with trains.
```

### Specialist Scope

#### Rail Specialist
- Track types (rail, electric, monorail, maglev)
- Signal placement and types (block, path, PBS)
- Station design (through stations, terminus, ro-ro)
- Train composition (engines, wagons, capacity)
- Junction design (avoiding deadlocks)
- Electrification planning

#### Road Specialist
- Road vs tram infrastructure
- Bus routes (passenger coverage)
- Truck routes (cargo delivery to industries)
- Road depot placement
- One-way road systems
- Town road networks

#### Sea Specialist
- Ship routes (long-haul bulk cargo)
- Dock placement (industry access)
- Canal construction (connecting waterways)
- Buoy placement (pathfinding)
- Ferry services (passengers over water)

#### Air Specialist
- Airport placement and sizing (small/city/international/intercontinental)
- Aircraft selection (speed vs capacity vs range)
- Passenger and mail routes (high-value, long-distance)
- Helicopter services (oil rigs, small towns)
- Hub-and-spoke vs point-to-point network design

---

### Round System (5-Minute Chunks)

Each game session is divided into **5-minute rounds**. This duration is tunable based on agent performance.

#### Round Structure

```
┌─────────────────────────────────────────────────────────────┐
│                      ROUND N (5 minutes)                     │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  PHASE 1: Coordinator Planning (30-60 seconds)               │
│  ├─ Review company finances                                  │
│  ├─ Check specialist reports from Round N-1                  │
│  ├─ Identify opportunities (new industries, town growth)     │
│  ├─ Allocate budgets for this round                          │
│  └─ Dispatch tasks to specialists                            │
│                                                              │
│  PHASE 2: Specialist Execution (3-4 minutes, parallel)       │
│  ├─ Rail Agent: Works on assigned rail tasks                 │
│  ├─ Road Agent: Works on assigned road tasks                 │
│  ├─ Sea Agent: Works on assigned sea tasks                   │
│  └─ Air Agent: Works on assigned air tasks                   │
│                                                              │
│  PHASE 3: Specialist Reports (30-60 seconds)                 │
│  ├─ Each specialist writes completion report                 │
│  ├─ Reports include: completed work, spend, issues, requests │
│  └─ Coordinator collects for next round planning             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### Round Timing Configuration

```bash
# Default: 5 minutes per round
ROUND_DURATION_SECONDS=300

# Shorter rounds for testing/fast-paced play
ROUND_DURATION_SECONDS=120  # 2 minutes

# Longer rounds for complex operations
ROUND_DURATION_SECONDS=600  # 10 minutes
```

#### Round State File

Each round, the coordinator writes `ROUND_STATE.json`:

```json
{
  "round": 7,
  "timestamp": "2025-12-25T15:30:00Z",
  "game_date": "1955-03-15",
  "company": {
    "cash": 1250000,
    "loan": 500000,
    "income_last_year": 320000
  },
  "budgets": {
    "rail": { "allocated": 400000, "spent": 0, "remaining": 400000 },
    "road": { "allocated": 150000, "spent": 0, "remaining": 150000 },
    "sea":  { "allocated": 100000, "spent": 0, "remaining": 100000 },
    "air":  { "allocated": 200000, "spent": 0, "remaining": 200000 },
    "reserve": 400000
  },
  "tasks": {
    "rail": [
      { "id": "R7-1", "priority": "high", "description": "Connect Coal Mine #5 to Steel Mill #2" },
      { "id": "R7-2", "priority": "medium", "description": "Add passing loop at Grenford Junction" }
    ],
    "road": [
      { "id": "D7-1", "priority": "high", "description": "Establish bus service in Tronbury (pop 2500+)" }
    ],
    "sea": [
      { "id": "S7-1", "priority": "low", "description": "Survey oil rig locations for future routes" }
    ],
    "air": [
      { "id": "A7-1", "priority": "medium", "description": "Build small airport at Dunthill for mail" }
    ]
  },
  "phase": "specialist_execution",
  "round_ends_at": "2025-12-25T15:35:00Z"
}
```

---

### Budget Management System

#### Budget Allocation Strategy

The coordinator allocates budget each round based on:

1. **Current cash position** - How much is available?
2. **ROI by transport type** - Which investments are paying off?
3. **Strategic priorities** - Early game vs late game focus
4. **Pending opportunities** - New industries, growing towns

#### Budget Categories

```
Total Available Cash
├── Operating Reserve (20%)     # Never touch - emergency fund
├── Rail Budget (variable)      # Typically 30-50% in early/mid game
├── Road Budget (variable)      # Typically 15-25%
├── Sea Budget (variable)       # Typically 5-15% (situational)
├── Air Budget (variable)       # Typically 10-20% (grows late game)
└── Unallocated (coordinator discretion)
```

#### Early Game Budget Template (Years 1-10)

```json
{
  "strategy": "rail_first",
  "rationale": "Rail has highest ROI for bulk cargo, establish coal/ore routes",
  "allocation": {
    "reserve_pct": 20,
    "rail_pct": 45,
    "road_pct": 20,
    "sea_pct": 5,
    "air_pct": 10
  }
}
```

#### Mid Game Budget Template (Years 10-30)

```json
{
  "strategy": "diversified_growth",
  "rationale": "Expand all networks, air becomes profitable for long routes",
  "allocation": {
    "reserve_pct": 15,
    "rail_pct": 35,
    "road_pct": 20,
    "sea_pct": 10,
    "air_pct": 20
  }
}
```

#### Late Game Budget Template (Years 30+)

```json
{
  "strategy": "optimization_and_air",
  "rationale": "Rail network mature, focus on air and optimization",
  "allocation": {
    "reserve_pct": 10,
    "rail_pct": 25,
    "road_pct": 15,
    "sea_pct": 15,
    "air_pct": 35
  }
}
```

#### Budget Request Protocol

Specialists can request additional budget mid-round:

```
RAIL_AGENT → COORDINATOR:
{
  "type": "budget_request",
  "agent": "rail",
  "amount": 150000,
  "justification": "Steel Mill #2 connection requires longer bridge than estimated",
  "priority": "high",
  "expected_roi": "45000/year"
}

COORDINATOR → RAIL_AGENT:
{
  "type": "budget_response",
  "approved": true,
  "amount": 150000,
  "source": "reallocated from air (airport can wait)",
  "note": "Complete the steel connection, it's our highest priority"
}
```

#### Budget Tracking CLI Commands

```bash
# View current budget state
ttdctl budget status

# Allocate budget (coordinator only)
ttdctl budget allocate rail 500000

# Request additional budget (specialists)
ttdctl budget request 150000 --reason "Bridge cost overrun"

# View spending history
ttdctl budget history --round 7

# Reallocate between specialists
ttdctl budget transfer --from air --to rail --amount 100000
```

---

### Specialist Reports

At the end of each round, specialists write a report to `reports/ROUND_N_<AGENT>.md`:

```markdown
# Rail Agent Report - Round 7

## Completed Tasks
- [R7-1] ✅ Connected Coal Mine #5 to Steel Mill #2
  - Track: 45 tiles, 2 bridges
  - Cost: $385,000
  - Trains deployed: 2x Class 66 + 8 coal wagons each

## In Progress
- [R7-2] 🔄 Passing loop at Grenford Junction
  - 60% complete, need 1 more signal
  - Will finish in Round 8

## Budget Status
- Allocated: $400,000
- Spent: $385,000
- Remaining: $15,000

## Issues
- None

## Requests for Next Round
- Additional train for Coal Mine #5 route ($25,000)
- Survey Iron Ore Mine #3 for potential connection

## Metrics
- Trains operating: 12 → 14
- Annual rail revenue: $180,000 → $215,000 (projected)
```

---

### Open Questions (Remaining)

1. **How do subagents communicate?**
   - Proposed: Coordinator-mediated via `ROUND_STATE.json` and report files
   - Specialists read tasks from state file, write reports

2. **Conflict resolution for geographic disputes?**
   - Coordinator assigns regions, or specialists flag conflicts for next round

3. **What if a specialist finishes early?**
   - Can request additional tasks from coordinator
   - Or work on "standing orders" (maintenance, optimization)

---

## Implementation Progress

### Phase 1: Foundation - COMPLETE ✅

1. [x] Set up build environment for OpenTTD
2. [x] Create basic JSON-RPC server (`src/rpc/rpc_server.cpp`)
3. [x] Create ttdctl CLI tool (`ttdctl/`)

### Phase 2: Core RPC Handlers - COMPLETE ✅

#### Query Handlers (Read-Only)
| Handler | Status | Description |
|---------|--------|-------------|
| `ping` | ✅ | Health check |
| `game.status` | ✅ | Calendar/economy year info |
| `company.list` | ✅ | All companies with finances |
| `vehicle.list` | ✅ | List vehicles by type |
| `vehicle.get` | ✅ | Get vehicle details |
| `station.list` | ✅ | List all stations |
| `station.get` | ✅ | Get station details with cargo |
| `industry.list` | ✅ | List all industries |
| `industry.get` | ✅ | Get industry details with production |
| `town.list` | ✅ | List all towns |
| `town.get` | ✅ | Get town details with ratings |
| `map.info` | ✅ | Map dimensions and settings |
| `map.distance` | ✅ | Manhattan distance between points |
| `map.scan` | ✅ | ASCII map visualization |
| `tile.get` | ✅ | Get tile type, height, owner |
| `tile.getRoadInfo` | ✅ | Road directions for depot placement |
| `order.list` | ✅ | Get vehicle orders |
| `engine.list` | ✅ | List available engines by type |
| `engine.get` | ✅ | Get engine details with refit options |
| `subsidy.list` | ✅ | Available subsidy opportunities |
| `cargo.list` | ✅ | All cargo types with properties |
| `cargo.getIncome` | ✅ | Calculate income for cargo route |
| `industry.getStockpile` | ✅ | Cargo stockpiled at industry |
| `industry.getAcceptance` | ✅ | What cargo an industry accepts |
| `station.getCargoPlanned` | ✅ | Cargo flow from link graph |
| `vehicle.getCargoByType` | ✅ | Cargo breakdown per type |
| `airport.info` | ✅ | Airport type specifications |

#### Action Handlers (Mutations)
| Handler | Status | Description |
|---------|--------|-------------|
| `vehicle.startstop` | ✅ | Toggle vehicle running state |
| `vehicle.depot` | ✅ | Send vehicle to depot |
| `vehicle.turnaround` | ✅ | Cancel depot order |
| `vehicle.build` | ✅ | Build new vehicle at depot |
| `vehicle.sell` | ✅ | Sell vehicle (must be in depot) |
| `vehicle.clone` | ✅ | Clone vehicle with orders |
| `vehicle.refit` | ✅ | Change cargo type |
| `order.append` | ✅ | Add order to end of list |
| `order.remove` | ✅ | Remove order by index |
| `order.insert` | ✅ | Insert order at position |
| `order.setFlags` | ✅ | Modify existing order flags |
| `order.share` | ✅ | Share orders between vehicles |
| `company.setLoan` | ✅ | Adjust loan amount |
| `town.performAction` | ✅ | Advertising, bribes, etc. |

#### Infrastructure Handlers
| Handler | Status | Description |
|---------|--------|-------------|
| `road.build` | ✅ | Build road pieces |
| `road.buildDepot` | ✅ | Build road depot |
| `road.buildStop` | ✅ | Build bus/truck stop |
| `rail.buildTrack` | ✅ | Build rail track |
| `rail.buildDepot` | ✅ | Build train depot |
| `rail.buildStation` | ✅ | Build train station |

#### Meta Handlers
| Handler | Status | Description |
|---------|--------|-------------|
| `game.newgame` | ✅ | Start new game with default settings |

### Phase 3: Camera/Viewport Control - COMPLETE ✅

For streaming support, added activity tracking and camera control:

#### Viewport Handlers
| Handler | Status | Description |
|---------|--------|-------------|
| `viewport.goto` | ✅ | Scroll viewport to tile coordinates |
| `viewport.follow` | ✅ | Follow a vehicle with the camera |
| `activity.hotspot` | ✅ | Get most active building area |
| `activity.clear` | ✅ | Clear activity history |

#### Activity Tracking
- Ring buffer (100 records, 60-second window) tracks where building happens
- `RpcRecordActivity()` called from infrastructure and vehicle build handlers
- Enables auto-camera that follows the action for streaming

### Not Yet Implemented

#### RPC Handlers Remaining
All infrastructure building handlers are now complete!

#### Phase 4: Terminal Window - NOT STARTED
- [ ] ShellProcess (PTY abstraction)
- [ ] TerminalSession (libvterm integration)
- [ ] AIAgentLaunch (spawn Claude)
- [ ] Terminal window GUI (`WC_AI_AGENT_TERMINAL`)

#### Phase 5: Multi-Agent System - NOT STARTED
- [ ] Round system (5-minute chunks)
- [ ] Coordinator + specialist agents
- [ ] Budget management
- [ ] Session files (ROUND_STATE.json)

### Files Created

```
src/rpc/
├── CMakeLists.txt              # Build configuration
├── rpc_server.cpp              # TCP server implementation
├── rpc_server.h                # Public interface
├── rpc_handlers.cpp            # Main registration
├── rpc_handlers.h              # Handler declarations
├── rpc_handlers_query.cpp      # Read-only query handlers
├── rpc_handlers_action.cpp     # Vehicle/order/company/town action handlers
├── rpc_handlers_infra.cpp      # Infrastructure building handlers
├── rpc_handlers_meta.cpp       # Game control handlers
└── rpc_handlers_viewport.cpp   # Camera control and activity tracking

ttdctl/
├── CMakeLists.txt
└── src/
    ├── main.cpp                # Command routing (123 lines)
    ├── cli_common.h            # Common types and declarations
    ├── cli_common.cpp          # Argument parsing and help text
    ├── rpc_client.h            # JSON-RPC client
    ├── rpc_client.cpp          # TCP client implementation
    ├── commands_query.cpp      # Query command handlers
    ├── commands_action.cpp     # Action command handlers
    ├── commands_infra.cpp      # Infrastructure command handlers
    └── commands_vehicle.cpp    # Vehicle/engine command handlers
```

---

## Quick Start Guide for AI Agents (Post-Compaction)

This section is for future Claude sessions that need to continue development or testing.

### Building OpenTTD

```bash
# Navigate to the OpenTTD directory
cd /Users/dlemmond/OpenTTD/OpenTTD

# Build with ninja (requires cmake already configured)
export PATH="/opt/homebrew/bin:$PATH"
ninja -C build

# Binaries are at:
# - Game: build/openttd
# - CLI:  build/ttdctl/ttdctl
```

### Starting the Game

```bash
# Option 1: Start with a saved game (recommended for testing)
/Users/dlemmond/OpenTTD/OpenTTD/build/openttd -g &

# Option 2: Start fresh and generate new world via RPC
/Users/dlemmond/OpenTTD/OpenTTD/build/openttd &
sleep 5
/Users/dlemmond/OpenTTD/OpenTTD/build/ttdctl/ttdctl game newgame
```

### Testing RPC Connection

```bash
# Test basic connectivity
/Users/dlemmond/OpenTTD/OpenTTD/build/ttdctl/ttdctl ping

# Should output: Connected to OpenTTD at localhost:9877
```

### Common Test Workflow

```bash
# 1. Check game status
ttdctl game status

# 2. List available engines
ttdctl engine list road

# 3. Build infrastructure (road + depot)
ttdctl road build 80 72 --pieces x
ttdctl road depot 80 73 --direction ne

# 4. Build a vehicle
ttdctl vehicle build --engine 116 --depot <tile_from_step_3>

# 5. Verify vehicle exists
ttdctl vehicle list

# 6. Sell the vehicle
ttdctl vehicle sell <vehicle_id>
```

### Killing OpenTTD

```bash
pkill -f openttd
```

### Key Files to Read

When resuming work:

1. **This file** - `AGENT_INTEGRATION_PLAN.md` - Overall status and architecture
2. **RPC handlers** - `src/rpc/rpc_handlers_*.cpp` - See implemented methods
3. **CLI tool** - `ttdctl/src/main.cpp` - See available commands
4. **RPC server** - `src/rpc/rpc_server.cpp` - TCP server implementation

### Git History

Recent commits related to this project:
```bash
git log --oneline -10
```

### Debugging Tips

1. **RPC not responding**: Game might not be running or might be on menu screen
2. **Method not found**: Game needs restart after rebuild to pick up new handlers
3. **Build errors**: Check includes - OpenTTD has specific patterns for string handling
4. **Company context**: Most actions require `Backup<CompanyID>` to switch company context

### Adding New RPC Handlers

1. Choose the appropriate file:
   - `rpc_handlers_query.cpp` - Read-only queries
   - `rpc_handlers_action.cpp` - Vehicle/order mutations
   - `rpc_handlers_infra.cpp` - Infrastructure building
   - `rpc_handlers_meta.cpp` - Game control

2. Add the handler function (static)
3. Register in the `RpcRegister*Handlers()` function at the bottom
4. Add CLI command to `ttdctl/src/main.cpp` if needed
5. Rebuild with `ninja -C build`
6. Restart OpenTTD to pick up changes

### ttdctl Full Command Reference

```bash
# Get help
ttdctl --help

# Connection
ttdctl ping

# Game
ttdctl game status
ttdctl game newgame [--seed N]

# Companies
ttdctl company list
ttdctl company setloan <amount>

# Engines
ttdctl engine list [road|train|ship|aircraft]
ttdctl engine get <id>

# Vehicles
ttdctl vehicle list [road|train|ship|aircraft]
ttdctl vehicle get <id>
ttdctl vehicle build --engine <id> --depot <tile>
ttdctl vehicle sell <id>
ttdctl vehicle clone <id> --depot <tile> [--share-orders]
ttdctl vehicle startstop <id>
ttdctl vehicle depot <id>
ttdctl vehicle turnaround <id>
ttdctl vehicle refit <id> --cargo <cargo_id>

# Stations
ttdctl station list
ttdctl station get <id>
ttdctl station flow <id>

# Industries
ttdctl industry list
ttdctl industry get <id>
ttdctl industry stockpile <id>
ttdctl industry acceptance <id>

# Towns
ttdctl town list
ttdctl town get <id>
ttdctl town action <town_id> --action <action_name>
# Actions: advertise_small, advertise_medium, advertise_large,
#          road_rebuild, build_statue, fund_buildings, buy_rights, bribe

# Map
ttdctl map info
ttdctl map distance <x1> <y1> <x2> <y2>
ttdctl map scan [--zoom N] [--traffic]

# Tiles
ttdctl tile get <x> <y>
ttdctl tile roadinfo <x> <y>

# Orders
ttdctl order list <vehicle_id>
ttdctl order append <vehicle_id> --station <id> [--load full] [--unload transfer]
ttdctl order insert <vehicle_id> --index <n> --station <id>
ttdctl order remove <vehicle_id> --index <n>
ttdctl order setflags <vehicle_id> --index <n> [--load full] [--unload transfer]
ttdctl order share <vehicle_id> <other_vehicle_id> --mode <share|copy|unshare>

# Road Infrastructure
ttdctl road build <x> <y> --pieces <x|y|all|nw|ne|sw|se>
ttdctl road depot <x> <y> --direction <ne|se|sw|nw>
ttdctl road stop <x> <y> --direction <ne|se|sw|nw> --type <bus|truck>

# Rail Infrastructure
ttdctl rail track <x> <y> --track <x|y|upper|lower|left|right>
ttdctl rail depot <x> <y> --direction <ne|se|sw|nw>
ttdctl rail station <x> <y> --axis <x|y> --platforms <n> --length <n>

# Subsidies
ttdctl subsidy list

# Cargo Economics
ttdctl cargo list
ttdctl cargo income <cargo_type> <distance> <days> [amount]

# Vehicle Cargo
ttdctl vehicle cargo <id>

# Airport Info
ttdctl airport info

# Camera/Viewport Control (for streaming)
ttdctl viewport goto <x> <y> [--instant]
ttdctl viewport follow <vehicle_id>
ttdctl viewport follow --stop
ttdctl activity hotspot [--seconds N]
ttdctl activity clear
```

