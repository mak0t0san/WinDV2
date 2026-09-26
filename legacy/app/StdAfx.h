// stdafx.h : precompiled header - system, MFC, ATL and DirectShow headers
#pragma once

#define VC_EXTRALEAN // Exclude rarely-used stuff from Windows headers

#include <afxwin.h>  // MFC core and standard components
#include <afxext.h>  // MFC extensions
#include <afxdisp.h> // MFC Automation classes
#include <afxcmn.h>  // MFC support for Windows Common Controls
#include <afxdlgs.h>

#include <atlbase.h> // CComPtr, CComQIPtr

#include <streams.h> // DirectShow base classes (external/baseclasses)

#include <algorithm>
#include <array>
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
