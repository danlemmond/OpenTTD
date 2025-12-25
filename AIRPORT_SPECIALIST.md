# Airport Specialist Agent Guide

This document contains the knowledge needed for an AI agent to successfully build and manage airports in OpenTTD.

## Airport Types and Sizes

All airports are anchored at the **top-left corner** of their footprint.

| Type | Size (WxH) | Available | Notes |
|------|------------|-----------|-------|
| small | 3x4 | 1930+ | Basic airport, good for starting |
| large | 4x5 | 1930+ | City airport |
| metropolitan | 5x5 | 1980+ | |
| international | 6x6 | 1990+ | |
| intercontinental | 9x11 | 2002+ | Largest airport |
| commuter | 4x4 | 1983+ | Small jets and props |
| heliport | 1x1 | 1963+ | Helicopters only |
| helidepot | 2x2 | 1976+ | Helicopter depot |
| helistation | 2x3 | 1980+ | Helicopter station |

## Building Requirements

For an airport at anchor position (x, y) with size WxH, ALL tiles in the rectangle must satisfy:

1. **Flat**: `is_flat: true` (slope = 0)
2. **Same height**: All tiles must have identical `height` value
3. **Buildable**: Type must be `clear`, `trees`, or `farmland` (not water, not owned structures)

### Tile Check Pattern

For a 3x4 small airport anchored at (x, y), check these 12 tiles:
```
Row 0: (x, y)   (x+1, y)   (x+2, y)
Row 1: (x, y+1) (x+1, y+1) (x+2, y+1)
Row 2: (x, y+2) (x+1, y+2) (x+2, y+2)
Row 3: (x, y+3) (x+1, y+3) (x+2, y+3)
```

## Search Algorithm

To find a valid airport location:

1. **Scan the map** using `ttdctl map scan` to identify areas with empty land (`.` symbols)
2. **Pick candidate anchor points** away from water (`~`), towns (`T`), and industries (`I`)
3. **Verify each tile** in the footprint using `ttdctl --output json tile get <x> <y>`
4. **Check conditions**:
   - All tiles have `is_flat: true`
   - All tiles have the same `height` value
   - No tiles are `water` type
5. **Build** with `ttdctl airport build <x> <y> --type <type>`

### Example: Finding a Small Airport Location

```bash
# 1. Scan map to find empty areas
ttdctl map scan 0 0 16 16

# 2. Check candidate anchor at (85, 85) - need 3x4 = 12 tiles
ttdctl --output json tile get 85 85  # Check height and is_flat
ttdctl --output json tile get 86 85
ttdctl --output json tile get 87 85
ttdctl --output json tile get 85 86
# ... check all 12 tiles ...

# 3. If all tiles are flat and same height, build
ttdctl airport build 85 85 --type small
```

### Successful Build Example

```
$ ttdctl airport build 85 85 --type small
Built small airport at tile 21845 (85, 85) (cost: 5595)
```

## Common Failure Reasons

| Error | Cause | Solution |
|-------|-------|----------|
| "check tile is flat land with enough space" | One or more tiles not flat | Check each tile individually; find different location |
| "check tile is flat land with enough space" | Tiles at different heights | All tiles must be same height |
| "check tile is flat land with enough space" | Water or owned tiles in footprint | Move to clear land |
| Cost = 0, success = false | Local authority refusal | Improve town rating or build elsewhere |

## CLI Commands

```bash
# Build airport
ttdctl airport build <x> <y> --type <type> [--station <id>]

# Check tile suitability
ttdctl --output json tile get <x> <y>

# Scan map for candidate areas
ttdctl map scan [x_cells] [y_cells] [width] [height]
```

## JSON Response Fields

When checking tiles with `--output json`:

```json
{
  "height": 2,        // Must be same for all tiles in footprint
  "is_flat": true,    // Must be true for all tiles
  "owner": 16,        // 16 = no owner (good), 0-14 = company owned
  "slope": 0,         // 0 = flat
  "tile": 21845,      // Tile index
  "type": "clear",    // clear, trees, farmland are buildable
  "x": 85,
  "y": 85
}
```

## Tips for Success

1. **Start with larger search area** - Don't give up on first failed attempt
2. **Check ALL tiles** - One non-flat tile will cause failure
3. **Prefer height 1-3** - Very high or very low areas tend to be hilly
4. **Avoid edges** - Map edges and coastlines are often sloped
5. **Use JSON output** - Structured data is easier to validate programmatically
