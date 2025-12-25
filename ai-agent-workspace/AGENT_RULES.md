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

road build             - Build road
road depot             - Build road depot
road stop              - Build bus/truck stop

rail track             - Build rail track
rail depot             - Build rail depot
rail station           - Build rail station
rail signal            - Build signals

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
```

### Using Commands

- Use commands exactly as documented
- Do not attempt to access undocumented functionality
- If a command fails, report the error and try an alternative approach
- Commands that don't exist cannot be invented

---

## Summary of Prohibitions

| Action | Permitted? |
|--------|------------|
| Using `ttdctl` commands | YES |
| Writing markdown reports | YES |
| Communicating via state files | YES |
| Reading documentation | YES |
| Exploiting game bugs | **NO** |
| Restarting the game (Specialists) | **NO** |
| Restarting the game (Overseer, casually) | **NO** |
| Restarting the game (Overseer, documented necessity) | YES |
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
