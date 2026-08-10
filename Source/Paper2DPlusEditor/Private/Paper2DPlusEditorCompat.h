// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "Misc/EngineVersionComparison.h" // ENGINE_MAJOR_VERSION / ENGINE_MINOR_VERSION
#include "UObject/UObjectGlobals.h"        // ERenameFlags / REN_* rename flags

// UE 5.8 deprecated REN_ForceNoResetLoaders (UObject::Rename no longer calls ResetLoaders, so the
// flag is a no-op there and trips C4996 — a hard error under -WarningsAsErrors). The plugin's
// rename-to-transient name-freeing pattern keeps the flag on 5.7 and earlier (where Rename still
// reset loaders) and drops it on 5.8+. Convention per docs/solutions/ue-cross-version-compat-patterns.md.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
	#define PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS (REN_DontCreateRedirectors | REN_ForceNoResetLoaders)
#else
	#define PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS (REN_DontCreateRedirectors)
#endif
