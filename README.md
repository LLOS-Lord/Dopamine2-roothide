# DarkSword & ClearSword for Dopamine2-roothide

## Files Included

```
Application/
├── Dopamine.xcodeproj/
│   └── project.pbxproj          # PATCHED - includes DarkSword & ClearSword targets
└── Dopamine/
    └── Exploits/
        ├── DarkSword/
        │   ├── DarkSword.h
        │   ├── DarkSword.m      # Patched for roothide
        │   └── Info.plist
        └── ClearSword/
            ├── ClearSword.h
            ├── ClearSword.m     # Patched for roothide
            ├── Info.plist
            └── exploit/         # Kernel exploit code (unchanged)
                ├── common.h
                ├── free_thread.c
                ├── krw.c
                ├── phys_oob.c
                ├── poc.c
                ├── socket.c
                ├── surface.c
                ├── utils.c
                └── ... (headers)
```

## Installation

Simply extract this ZIP into your Dopamine2-roothide repo root:

```bash
cd Dopamine2-roothide
unzip /path/to/roothide_darksword_clearsword_complete.zip
```

Then open `Application/Dopamine.xcodeproj` and build. DarkSword and ClearSword will appear in the exploit picker.

## Changes Summary

### DarkSword.m
- Removed `gPrimitives.krwMinSafeReadSize` (roothide compat)
- Added A8 heap stabilization workaround
- Updated `exploit_deinit` to NULL primitives + close sockets

### ClearSword.m
- Removed `gPrimitives.krwMinSafeReadSize`
- Added A8 heap stabilization workaround
- Updated `exploit_deinit` to NULL primitives
- Added `sys/sysctl.h` and `stdlib.h` includes

### project.pbxproj
- Added DarkSword.framework target
- Added ClearSword.framework target
- Added Embed Frameworks entries for both
- Added PBXTargetDependency entries

## Compatibility
- iOS 15.0 - 16.7.15
- roothide 2.x
