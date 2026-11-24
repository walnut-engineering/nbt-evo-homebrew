# Window/Surface/Layer ID Coordination Analysis

## Critical Finding: EarlyVideoApp Working Pattern

Based on systematic analysis of `startup_slog.log`, the working `EarlyVideoApp` follows a specific pattern for window/surface/layer coordination that our unified library must replicate.

## EarlyVideoApp Surface/Layer Creation Pattern

### Step 1: ILM Layer Management Setup
```
1. ilm_init - Initialize ILM connection
2. ilm_layerAddSurface - Add surfaces to layers BEFORE creating screen windows
3. ilm_commitChanges - Commit each layer addition individually
```

### Step 2: Surface and Layer IDs Used by EarlyVideoApp
```
Surface IDs: 32, 65792, 65537, 65539, 65538
Layer IDs:   2147811328, 2148532224, 2148663296, 2148597760

Layer Assignment Pattern:
- Layer 2147811328: surfaces 32, 65792
- Layer 2148532224: surface 65537  
- Layer 2148663296: surface 65539
- Layer 2148597760: surface 65538
```

### Step 3: Window Creation After ILM Setup
```
Created windows:
- Window 32@20c1d8: buffer [1440x540], visibility = 0 initially
- Window 65792@20c260: buffer [1536x640], visibility = 0 initially
```

### Step 4: Surface Visibility Management
```
1. Set surfaces visible individually: ilm_surfaceSetVisibility(surface_id, 1)
2. Commit changes after each visibility change
3. Order: 65539, 65538, 65537, then finally 32
```

### Step 5: Display Render Order
```
ilm_displaySetRenderOrder(0): 2147811328 2148597760 2148663296 2148532224
```

### Step 6: Layer Visibility
```
ilm_layerSetVisibility(2147811328, 1) - Make primary layer visible
```

## Critical Integration Requirements

### 1. Pre-Create ILM Structure
- **MUST** call `ilm_layerAddSurface` BEFORE creating screen windows
- Each surface must be assigned to its layer before the screen window exists

### 2. Surface ID Selection
- EarlyVideoApp uses specific ranges: 32, 65792, 65537, 65539, 65538
- These appear to be pre-allocated/coordinated surface IDs
- Our unified library should use similar ID ranges or coordinate with system

### 3. Layer ID Selection
- Layer IDs are large numbers: 2147811328, 2148532224, 2148663296, 2148597760
- These appear to be system-defined layer identifiers
- Layer 2147811328 seems to be the primary visible layer

### 4. Window ID String Coordination
- The SCREEN_PROPERTY_ID_STRING must match the surface_id exactly
- This links the screen window to the ILM surface

### 5. Visibility Sequencing
- Windows created with visibility=0 initially
- Surfaces made visible through ILM calls, not screen calls
- Layer visibility controls overall display

## Working EarlyVideoApp Sequence

```
Time: 00:00:01
1. ilm_init() - Connect to Layer Manager
2. ilm_layerAddSurface(2147811328, 32) + commit
3. ilm_layerAddSurface(2147811328, 65792) + commit  
4. ilm_layerAddSurface(2148532224, 65537) + commit
5. ilm_layerAddSurface(2148663296, 65539) + commit
6. ilm_layerAddSurface(2148597760, 65538) + commit
7. Set surface visibility: 65539, 65538, 65537
8. ilm_displaySetRenderOrder(0, [2147811328, 2148597760, 2148663296, 2148532224])
9. ilm_layerSetVisibility(2147811328, 1) - Enable primary layer
10. Create screen windows with matching surface IDs as SCREEN_PROPERTY_ID_STRING
11. Make surfaces visible: surface 32, then 65792
```

## Unified Library Required Changes

### 1. Layer Management Integration
Our unified library needs to:
- Initialize ILM connection early in context creation
- Pre-create layer structure before screen window creation
- Use appropriate layer/surface ID coordination

### 2. Surface ID Strategy
Options:
- Use fixed surface IDs like EarlyVideoApp (32, 65792, etc.)
- Request surface ID allocation from system
- Use deterministic surface ID generation

### 3. Layer Assignment
- Use layer 2147811328 as primary display layer (matches EarlyVideoApp)
- Follow the established render order pattern

### 4. Implementation Priority
1. **CRITICAL**: Add ILM layer setup before screen window creation
2. **CRITICAL**: Set SCREEN_PROPERTY_ID_STRING to match surface_id
3. **HIGH**: Use established layer/surface ID patterns
4. **HIGH**: Implement proper visibility sequencing

## Next Steps

1. Modify `evo_context_unified.c` to initialize ILM and create layer structure
2. Update `evo_display_unified.c` to use coordinated surface/layer IDs
3. Test with the established ID patterns from EarlyVideoApp
4. Verify video output appears correctly

## Key Insight

The working system creates the ILM layer/surface relationships FIRST, then creates screen windows that reference those pre-existing surface IDs. Our unified library was creating screen windows independently without proper ILM coordination.
