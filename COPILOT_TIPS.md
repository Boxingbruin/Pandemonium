# Pandemonium N64 Development - Copilot Tips

## Project Overview
Pandemonium is an N64 game built using LibDragon and the T3D 3D engine. The project structure follows a modular design with separate systems for graphics, audio, input, and game objects.

## Key Architecture Patterns

### File Organization
- `src/main.c` - Core game loop and initialization
- `src/controllers/` - System controllers (camera, audio)
- `src/objects/` - Game entities (character, boss)
- `src/scenes/` - Scene management and rendering
- `src/utilities/` - Shared utility functions
- `src/dev/` - Development tools and debugging
- `filesystem/` - Assets accessible via DFS (rom:/)

### Asset Loading
- Models: `t3d_model_load("rom:/path/model.t3dm")`
- Sprites: Access via sprite system
- All assets must be in `filesystem/` directory
- Use DFS paths with `rom:/` prefix

## Current Game State

### Implemented Systems
- ✅ Core game loop with fixed timestep
- ✅ Camera system with multiple modes (character, freecam, etc.)
- ✅ Lighting system with directional and ambient lights
- ✅ Audio controller for music and SFX
- ✅ Input handling via joypad utility
- ✅ Development tools and debug overlay
- ✅ Character object system (knight model)
- ✅ Boss object system (placeholder)

### Scene System
- `scene_init()` - Initialize game objects and systems
- `scene_update()` - Per-frame logic updates
- `scene_fixed_update()` - Fixed timestep updates
- `scene_draw()` - Rendering pipeline
- `scene_cleanup()` - Resource cleanup

### Character System
- Knight character at `filesystem/knight/knight64.t3dm`
- Supports animations (currently disabled)
- Has collision system structure (capsule collider)
- Spawns at world origin (0,0,0)
- Uses MODEL_SCALE (0.0625f) for consistent sizing

## Development Workflow

### Building
```bash
make clean && make
```

### Running/Testing
- Launch emulator: `$ARES pandemonium.z64`
- Emulator executable available via $ARES environment variable

### Asset Pipeline
- 3D models: `.t3dm` format via T3D tools
- Textures: Various formats supported by LibDragon
- Place all assets in `filesystem/` directory

### Debug Features
- DEV_MODE flag enables debug tools
- Press Z to toggle dev menu
- Camera switching (freecam, character follow)
- Performance metrics display
- Debug drawing utilities

## Common Patterns

### Adding New Game Objects
1. Create header in `src/objects/object_name.h`
2. Implement in `src/objects/object_name.c`
3. Follow existing patterns from character.c/boss.c
4. Add init/update/draw/cleanup functions
5. Integrate into scene system

### Memory Management
- Use `malloc_uncached()` for matrices
- Use `rspq_block_begin/end()` for draw lists
- Always pair init with cleanup functions
- Check for null pointers before freeing

### 3D Transformations
- Use T3D math library for vectors/matrices
- `t3d_mat4fp_from_srt_euler()` for SRT transforms
- `t3d_matrix_set()` before drawing objects
- Use matrix stack for hierarchical transforms

## Known Issues & TODOs

### Code TODOs
- `main.c:100` - Fix update/draw order
- `main.c:127` - Clean up dev area code
- `camera_controller.h:16` - Convert globals to struct
- `scene.c:19` - Move dev.h include to proper location

### Missing Features
- Player input handling for character movement
- Animation system integration
- Collision detection implementation
- Boss AI and behavior
- Game state management
- Level/world loading system

## Performance Considerations

### N64 Hardware Limitations
- 4MB RAM total
- Limited polygon budget (~1000-2000 per frame)
- Texture memory constraints
- Use MODEL_SCALE to keep models reasonable size

### Optimization Tips
- Use RSP blocks for repeated draw calls
- Minimize texture switches
- Use LOD for distant objects
- Profile with dev tools (rspq_profile)

## Camera System Guide

### Camera States
- `CAMERA_CHARACTER` - Follow character
- `CAMERA_FREECAM` - Free movement (dev mode)
- `CAMERA_FIXED` - Fixed position
- `CAMERA_CUSTOM` - Custom positioning

### Camera Setup
Current setup positions camera to view character at origin. Adjust in `scene_init()` if character spawn point changes.

## Asset Requirements

### 3D Models
- Use T3D format (.t3dm)
- Include proper UV mapping
- Keep polygon count reasonable for N64
- Test with actual hardware if possible

### Recommended Next Steps
1. Add player input to move character
2. Implement basic character animations
3. Add collision detection
4. Create simple level geometry
5. Implement boss behavior

## Debugging Tips

### Common Issues
- Black screen: Check model paths and lighting
- Performance drops: Check polygon count and draw calls
- Memory leaks: Ensure all malloc/free pairs match
- Asset not found: Verify filesystem paths

### Dev Tools Usage
- Toggle metrics with dev menu
- Use debug drawing for collision visualization
- Profile RSP usage for performance bottlenecks
- Monitor heap usage for memory issues