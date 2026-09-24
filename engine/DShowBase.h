// DShowBase.h : system headers for the DirectShow engine
//
// The engine uses Win32, ATL and the DirectShow base classes but no MFC, so it
// can be linked into both the MFC app and the WinDV.Native DLL. Every engine
// header includes this first; it is also the engine's precompiled header.
#pragma once

#include <windows.h>

#include <atlbase.h> // CComPtr, CComQIPtr

#include <streams.h> // DirectShow base classes (external/baseclasses)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
