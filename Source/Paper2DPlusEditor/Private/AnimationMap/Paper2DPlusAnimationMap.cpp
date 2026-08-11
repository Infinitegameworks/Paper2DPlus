// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/Paper2DPlusAnimationMap.h"

// All behavior lives in the header: the delegate seam is plain members the panel binds (U4), and the
// shared node-spawn funnel is a template (SpawnNodeUntransactional — the ClearFlags(RF_Transactional)
// ordering invariant). This TU exists so the class has a home translation unit alongside its
// UHT-generated code.
