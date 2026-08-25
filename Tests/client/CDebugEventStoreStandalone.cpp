/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Tests/client/CDebugEventStoreStandalone.cpp
 *  PURPOSE:     Build the DevTools store without the client core PCH
 *
 *****************************************************************************/

#include "../../Client/core/CDebugEventStore.h"

// Keep unit tests on the production implementation without pulling the core's
// Windows/CEF precompiled header into this headless executable.
#include "../../Client/core/CDebugEventStore.inl"
