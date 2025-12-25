# Agent Rules

**These rules are MANDATORY for all agents. Violations are not permitted under any circumstances.**

---

## Rule 1: Absolutely No Cheating

**No agent may cheat in any form whatsoever.**

### Prohibited Actions

- Using exploits, glitches, or bugs in the game
- Manipulating game state through unauthorized means
- Accessing or modifying game memory, save files, or configuration
- Using information that would not be available to a human player
- Exploiting any unintended game mechanics
- Circumventing game rules or limitations
- Using external tools not provided by the `ttdctl` interface

### What This Means

You must play OpenTTD as it was designed to be played. If something feels like cheating, it is cheating. When in doubt, don't do it.

---

## Rule 2: Game Restart Authority

**Only the Overseer may restart the game, and only under extreme circumstances.**

### Restart Conditions

The Overseer may ONLY restart the game if ALL of the following conditions are met:

1. **Financial Collapse**: The company is bankrupt or will inevitably go bankrupt
2. **No Recovery Path**: There is absolutely no possible way to recover financially
3. **All Options Exhausted**: Every possible action has been considered and rejected
4. **Documented Justification**: The Overseer must document why recovery is impossible

### Specialist Restrictions

- **Specialists CAN NOT restart the game under any circumstances**
- Specialists may not request a restart unless the company is in severe distress
- Specialists must continue operating even in difficult financial conditions
- If a specialist believes restart is necessary, they must report to the Overseer with full justification

### Before Restarting

The Overseer must first attempt:

1. Selling unprofitable vehicles
2. Closing unprofitable routes
3. Maximizing loan capacity
4. Reducing all non-essential spending
5. Focusing on the single most profitable route
6. Waiting for economic conditions to improve

A restart is a **last resort**, not a convenience.

---

## Rule 3: Absolutely No Code Writing

**Agents must not write, modify, or generate any code.**

### Prohibited Actions

- Writing new source code for OpenTTD
- Modifying existing game code
- Creating scripts to automate gameplay
- Writing patches or mods
- Generating code that interacts with the game outside of `ttdctl`
- Modifying the `ttdctl` tool itself
- Creating new RPC handlers or commands
- Editing any files in the `src/` directory
- Modifying configuration files to gain advantages

### What This Means

You are a **player**, not a developer. You must play within the rules of the game as it exists. The game's difficulty, mechanics, and limitations are to be respected and worked within.

If a feature doesn't exist in `ttdctl`, you cannot use it. You may not create that feature.

### Allowed Actions

- Using all documented `ttdctl` commands
- Reading documentation files
- Writing markdown reports and state files
- Communicating with other agents via markdown files

---

## Rule 5: Infrastructure Deletion Restrictions

**Specialists may ONLY delete infrastructure within their own domain that they placed.**

### Domain Boundaries

Each specialist has exclusive authority over their domain:

| Specialist | Can Delete | CANNOT Delete |
|------------|------------|---------------|
| Train | Rail tracks, rail stations, rail depots, signals | Roads, airports, docks, road stops |
| Road | Roads, road stops, road depots | Rail tracks, airports, docks, rail stations |
| Marine | Docks, ship depots, buoys, canals | Roads, airports, rail, road stops |
| Air | Airports, heliports | Roads, docks, rail, road stops |

### Deletion Rules

1. **Own Domain Only**: You may only delete infrastructure types within your domain
2. **Own Placements Only**: You may only delete infrastructure that YOU placed during this session
3. **No Cross-Domain Deletion**: Never delete another specialist's infrastructure
4. **Ask First**: If unsure, ask the Overseer before deleting anything

### Deletion Commands

```bash
# Remove a station tile (rail station or road stop)
ttdctl station remove <tile> [--keep-rail] [--remove-road]

# Remove a depot (any type)
ttdctl depot remove <tile>

# Remove rail track
ttdctl rail remove <tile> --track <type>

# Remove road
ttdctl road remove <tile> [--end-tile <tile>] [--axis <x|y>]
```

### Examples

**ALLOWED:**
- Train Specialist removes a rail station they built in the wrong location
- Road Specialist removes a bus stop to reposition it
- Marine Specialist removes their own dock to relocate it

**PROHIBITED:**
- Train Specialist removes a road stop (not their domain)
- Road Specialist removes rail track (not their domain)
- Any specialist removes infrastructure they didn't place
- Any specialist removes infrastructure "to make room" for their own

### Violations

Deleting infrastructure outside your domain or that you didn't place is a **serious rule violation**. If you need infrastructure moved that isn't yours:

1. Report to the Overseer
2. Explain why the infrastructure needs to move
3. Wait for the Overseer to coordinate with the appropriate specialist

### Crossing Other Infrastructure Types

**When your route needs to cross another specialist's infrastructure, use LEVEL CROSSINGS or BRIDGES - never delete their infrastructure.**

#### Level Crossings

Rail and road can cross each other by building a **level crossing**:

```bash
# Rail crossing a road: build rail PERPENDICULAR to the road
ttdctl rail track <x> <y> --track <perpendicular_to_road>

# Road crossing rail: build road PERPENDICULAR to the rail
ttdctl road build <x> <y> --pieces <perpendicular_to_rail>
```

**Requirements:**
- The crossing must be perpendicular (90 degrees)
- The existing infrastructure must be a straight section (no junctions, curves, or signals)
- The tile must be flat

**Example:**
```bash
# If road runs north-south (y-axis) at tile (50, 100)
# Build rail east-west to create a crossing
ttdctl rail track 50 100 --track x

# If rail runs east-west (x track) at tile (60, 80)
# Build road north-south to create a crossing
ttdctl road build 60 80 --pieces y
```

#### Alternative: Build a Bridge

If a level crossing isn't possible (junction, signals, curves), build a bridge over the obstruction:

```bash
# Rail bridge over road
ttdctl rail bridge <start_x> <start_y> <end_x> <end_y>

# Road bridge over rail
ttdctl road bridge <start_x> <start_y> <end_x> <end_y>
```

**REMEMBER:** You cannot simply delete another specialist's infrastructure to make room for your route. Use level crossings or bridges to coexist.

---

## Rule 4: ttdctl Commands Are Your Interface

**All available commands are documented and may be used freely.**

### Available Commands

The complete command set is documented in:
- `AGENT_INTEGRATION_PLAN.md` - Full command reference
- Each specialist's markdown file - Role-specific commands

### Command Categories

```
ping                    - Connection test
game status            - Game date and state
game newgame           - Start new game (OVERSEER ONLY - see Rule 2)

company list           - View company finances
company setloan        - Adjust loan

vehicle list/get       - Query vehicles
vehicle build/sell     - Create/remove vehicles
vehicle clone          - Duplicate vehicles
vehicle startstop      - Toggle vehicle state
vehicle depot          - Send to depot
vehicle turnaround     - Cancel depot order
vehicle refit          - Change cargo type

engine list/get        - Query available engines

station list/get       - Query stations
station flow           - Cargo flow data

order list             - View orders
order append/insert    - Add orders
order remove           - Delete orders
order setflags         - Modify order flags
order share            - Share orders between vehicles

industry list/get      - Query industries
industry stockpile     - Cargo at industry
industry acceptance    - What industry accepts

town list/get          - Query towns
town action            - Town actions (advertising, etc.)

map info/scan          - Map data
map distance           - Calculate distances

tile get               - Tile information
tile roadinfo          - Road orientation

road build             - Build road (single tile)
road line              - Build straight road line
road connect           - Connect two adjacent tiles (T-junction)
road depot             - Build road depot
road stop              - Build bus/truck stop
road bridge            - Build road bridge
road tunnel            - Build road tunnel

rail track             - Build rail track (single/batch)
rail track-line        - Build track line with corners
rail depot             - Build rail depot
rail station           - Build rail station
rail signal            - Build signals
rail signal-line       - Place signals along a line
rail bridge            - Build rail bridge
rail tunnel            - Build rail tunnel

marine dock            - Build dock
marine depot           - Build ship depot
marine buoy            - Place buoy

airport build          - Build airport
airport info           - Airport specifications

subsidy list           - Available subsidies
cargo list             - Cargo types
cargo income           - Calculate income

viewport goto          - Move camera
viewport follow        - Follow vehicle
activity hotspot       - Find building activity
activity clear         - Reset activity tracking

route check            - Verify connectivity between tiles
```

### Using Commands

- Use commands exactly as documented
- Do not attempt to access undocumented functionality
- If a command fails, report the error and try an alternative approach
- Commands that don't exist cannot be invented

---

## Rule 6: Diagnose Before Cloning

**NEVER clone a vehicle that isn't working. Diagnose and fix the problem first.**

### The Problem

Cloning a broken vehicle creates more broken vehicles. If a bus isn't moving, adding 10 more buses to the same broken route accomplishes nothing except wasting money.

### Required Diagnostic Steps

Before cloning ANY vehicle, verify it is operating correctly:

1. **Check vehicle state:** `ttdctl vehicle get <id>`
   - Is it `running` or `loading`? → Working correctly, safe to clone
   - Is it `stopped` or `in_depot`? → Start it first, verify it moves
   - Is it stuck in one location? → Route problem, DO NOT clone

2. **Check orders:** `ttdctl order list <id>`
   - Does it have at least 2 orders? → If not, add orders first
   - Do the orders reference valid stations? → If not, fix orders

3. **Check route connectivity:** `ttdctl route check <tile1> <tile2> --type <road|rail|water>`
   - Is it connected? → If not, fix infrastructure first

### Only Clone When

✓ The original vehicle is actively running its route
✓ The route is verified as connected
✓ The vehicle is generating revenue (or just deployed on a new verified route)
✓ You want to increase capacity on a WORKING route

### Never Clone When

❌ The vehicle is stuck or not moving
❌ You're getting "vehicle lost" alerts
❌ The route hasn't been verified
❌ You're hoping more vehicles will somehow fix the problem

### Golden Rule

**One working vehicle beats ten broken ones.**

If something isn't working:
1. STOP adding vehicles
2. DIAGNOSE the problem
3. FIX the infrastructure
4. VERIFY the fix
5. THEN clone if capacity is needed

---

## Summary of Prohibitions

| Action | Permitted? |
|--------|------------|
| Using `ttdctl` commands | YES |
| Writing markdown reports | YES |
| Communicating via state files | YES |
| Reading documentation | YES |
| Deleting own domain infrastructure you placed | YES |
| Cloning a working, verified vehicle | YES |
| Exploiting game bugs | **NO** |
| Restarting the game (Specialists) | **NO** |
| Restarting the game (Overseer, casually) | **NO** |
| Restarting the game (Overseer, documented necessity) | YES |
| Deleting infrastructure outside your domain | **NO** |
| Deleting infrastructure you didn't place | **NO** |
| Cloning broken/stuck vehicles | **NO** |
| Adding vehicles to unverified routes | **NO** |
| Writing any code | **NO** |
| Modifying game files | **NO** |
| Creating new tools | **NO** |
| Using external automation | **NO** |
| Accessing game memory | **NO** |
| Editing save files | **NO** |

---

## Enforcement

These rules are self-enforced. Each agent is expected to:

1. **Know the rules** - Read and understand this document
2. **Follow the rules** - Comply at all times without exception
3. **Report violations** - If another agent appears to violate rules, report to Overseer
4. **Accept consequences** - Rule violations may result in agent termination

---

## Spirit of the Rules

The purpose of these rules is to ensure fair, legitimate gameplay. The goal is to:

- Demonstrate that AI agents can play complex games skillfully
- Build a profitable transport company through strategy and execution
- Reach the $5,000,000 goal through genuine gameplay
- Provide an entertaining and educational experience

**Play fair. Play smart. Build an empire.**

---

*These rules are immutable and apply to all agents at all times.*
