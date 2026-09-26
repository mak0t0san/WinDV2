// NativeApi.cpp : the flat C API (windv_api.h) over DVEngine

#include "DShowBase.h"
#include "windv_api.h"

#include "CaptureNaming.h"
#include "CommandLine.h"
#include "DShowError.h"
#include "DVDevice.h"
#include "DVEngine.h"
#include "EngineThread.h"
#include "TimeFormat.h"
extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {

constexpr wchar_t kPreviewClass[] = L"WinDVPreview";
constexpr std::int32_t kQueueCapacity = 100;

// Copies text into a caller's buffer; returns the length needed including the
// null, and writes nothing if the buffer is too small.
std::int32_t CopyOut(const std::wstring& text, wchar_t* buffer, std::int32_t length)
{
	const auto needed = static_cast<std::int32_t>(text.size() + 1);
	if (buffer && length >= needed) {
		std::copy_n(text.c_str(), needed, buffer);
	}
	return needed;
}

std::wstring Arg(const wchar_t* text)
{
	return text ? std::wstring(text) : std::wstring();
}

} // namespace

struct windv_engine final : private DVEngineEvents {
	windv_engine(HWND parent, windv_event_callback callback, void* context);
	~windv_engine();

	// Runs action on the engine thread; exceptions become a result code plus
	// the message for windv_take_error.
	template <typename Action>
	windv_result Call(Action&& action)
	{
		windv_result result = WINDV_OK;
		m_thread->Invoke([&] {
			try {
				if (!m_engine) {
					throw DShowError(L"The engine is not running");
				}
				action(*m_engine);
			} catch (const DShowError& e) {
				SetError(e.Message());
				result = e.GetCause() == DShowError::Cause::DeviceNotFound ? WINDV_DEVICE_NOT_FOUND : WINDV_FAILED;
			} catch (const std::exception& e) {
				SetError(Widen(e.what()));
				result = WINDV_FAILED;
			} catch (...) {
				SetError(L"Unexpected error");
				result = WINDV_FAILED;
			}
		});
		return result;
	}

	void SetError(const std::wstring& message)
	{
		std::scoped_lock lock(m_mutex);
		m_callError = message;
	}

	std::wstring TakeError()
	{
		{
			std::scoped_lock lock(m_mutex);
			if (!m_callError.empty()) {
				return std::exchange(m_callError, {});
			}
		}
		return m_engine ? m_engine->TakeError() : std::wstring();
	}

	void GetStatus(windv_status* status)
	{
		*status = {};
		{
			std::scoped_lock lock(m_mutex);
			*status = m_status;
		}
		if (m_engine) {
			// The engine's own counters are atomics and can be read from here.
			status->state = m_engine->GetState();
			status->dropped = m_engine->GetDropped();
			status->counter = m_engine->GetCounter();
			status->time = m_engine->GetTime();
			status->dvTime = m_engine->GetDVTime();
			status->framesReceived = m_engine->GetFramesReceived();
			status->stopReason = static_cast<std::int32_t>(m_engine->GetStopReason());
			status->fileFrameCount = m_engine->GetFileFrameCount();
		}
	}

	void MovePreview(int x, int y, int width, int height)
	{
		if (width <= 0 || height <= 0) {
			ShowWindow(m_preview, SW_HIDE);
			return;
		}
		SetWindowPos(m_preview, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	std::unique_ptr<EngineThread> m_thread;
	std::unique_ptr<DVEngine> m_engine; // created, used and destroyed on m_thread

private:
	static LRESULT CALLBACK PreviewProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Engine thread, between tasks: refresh what the UI polls, and keep the
	// display (and while tape runs, the machine) awake.
	void Tick();

	void OnDVTimeChanged() override { Notify(WINDV_EVENT_DV_TIME_CHANGED); }
	void OnError() override { Notify(WINDV_EVENT_ERROR); }

	void Notify(windv_event event)
	{
		if (m_callback) {
			m_callback(m_context, event);
		}
	}

	HWND m_preview = nullptr;
	windv_event_callback m_callback;
	void* m_context;

	std::mutex m_mutex; // guards m_status and m_callError
	windv_status m_status{};
	std::wstring m_callError;
};

windv_engine::windv_engine(HWND parent, windv_event_callback callback, void* context)
    : m_callback(callback), m_context(context)
{
	static std::once_flag registered;
	const HINSTANCE instance = reinterpret_cast<HINSTANCE>(&__ImageBase);
	std::call_once(registered, [instance] {
		WNDCLASSEXW wc{sizeof(wc)};
		wc.lpfnWndProc = PreviewProc;
		wc.hInstance = instance;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		wc.lpszClassName = kPreviewClass;
		RegisterClassExW(&wc);
	});

	// Layered, so the compositor gives it (and the renderer's window inside it)
	// a surface of its own. WinUI's top-level window is created without a GDI
	// redirection surface, so an ordinary child window would never show the
	// renderer's GDI/DirectDraw output. Needs Windows 8+ (layered child windows).
	m_preview = CreateWindowExW(WS_EX_LAYERED, kPreviewClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
	                            0, 0, parent, nullptr, instance, nullptr);
	if (!m_preview) {
		throw DShowError(L"Can't create the preview window", HRESULT_FROM_WIN32(GetLastError()));
	}
	SetLayeredWindowAttributes(m_preview, 0, 255, LWA_ALPHA); // fully opaque
	SetWindowLongPtrW(m_preview, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	m_status.counter = -1;
	m_status.time = -1;
	m_status.queueCapacity = kQueueCapacity;

	m_thread = std::make_unique<EngineThread>([this] { Tick(); });
	m_thread->Invoke([this] {
		m_engine = std::make_unique<DVEngine>(static_cast<DVEngineEvents*>(this));
		m_engine->SetPreviewWindow(m_preview);
	});
}

windv_engine::~windv_engine()
{
	m_thread->Invoke([this] { m_engine.reset(); });
	m_thread.reset();
	SetWindowLongPtrW(m_preview, GWLP_USERDATA, 0);
	DestroyWindow(m_preview);
}

void windv_engine::Tick()
{
	if (!m_engine) {
		return;
	}

	const DVEngine::State state = m_engine->GetState();
	windv_status status{};
	status.state = state;
	status.deckMode = static_cast<std::int32_t>(m_engine->GetDeckMode());
	status.canControlDeck = m_engine->CanControlDeck() ? 1 : 0;
	status.queueLoad = static_cast<std::int32_t>(m_engine->GetQueueLoad());
	status.queueCapacity = kQueueCapacity;
	{
		std::scoped_lock lock(m_mutex);
		m_status = status;
	}

	if (state != DVEngine::Idle) {
		const bool active = state == DVEngine::Capturing || state == DVEngine::Recording;
		SetThreadExecutionState(ES_DISPLAY_REQUIRED | (active ? ES_SYSTEM_REQUIRED : 0));
	}
}

LRESULT CALLBACK windv_engine::PreviewProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_SIZE) {
		if (auto* self = reinterpret_cast<windv_engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA))) {
			self->m_thread->Post([self] {
				if (self->m_engine) {
					self->m_engine->ResizePreview();
				}
			});
		}
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////
// Exports

WINDV_API windv_result WINDV_CALL windv_create(void* parentHwnd, windv_event_callback callback, void* context,
                                               windv_handle* engine)
{
	if (!engine || !parentHwnd) {
		return WINDV_INVALID_ARGUMENT;
	}
	*engine = nullptr;
	try {
		// Capture must keep up with the camcorder in real time.
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		*engine = new windv_engine(static_cast<HWND>(parentHwnd), callback, context);
		return WINDV_OK;
	} catch (...) {
		return WINDV_FAILED;
	}
}

WINDV_API void WINDV_CALL windv_destroy(windv_handle engine)
{
	delete engine;
}

WINDV_API windv_result WINDV_CALL windv_list_devices(windv_handle engine, wchar_t* buffer, std::int32_t length,
                                                     std::int32_t* needed)
{
	if (!engine || !needed) {
		return WINDV_INVALID_ARGUMENT;
	}
	std::wstring list;
	const windv_result result = engine->Call([&](DVEngine&) {
		for (const std::wstring& device : GetVideoDeviceList()) {
			if (!list.empty()) {
				list += L'\n';
			}
			list += device;
		}
	});
	if (result != WINDV_OK) {
		return result;
	}
	*needed = CopyOut(list, buffer, length);
	return *needed > length ? WINDV_BUFFER_TOO_SMALL : WINDV_OK;
}

WINDV_API void WINDV_CALL windv_preview_move(windv_handle engine, std::int32_t x, std::int32_t y, std::int32_t width,
                                             std::int32_t height)
{
	if (engine) {
		engine->MovePreview(x, y, width, height);
	}
}

WINDV_API void WINDV_CALL windv_set_options(windv_handle engine, const windv_options* options)
{
	if (!engine || !options || !engine->m_engine) {
		return;
	}
	DVEngine& e = *engine->m_engine;
	e.m_type2AVI = options->type2AVI != 0;
	e.m_discontinuityThreshold = (std::max)(0, options->discontinuityThreshold);
	e.m_maxAVIFrames = (std::max)(10, options->maxAVIFrames);
	e.m_everyNth = (std::max)(1, options->everyNth);
	e.m_recordPreview = options->recordPreview != 0;
	e.m_DVctrl = options->deckFollowsPipeline != 0;
	e.m_signalLossSeconds = (std::max)(0, options->signalLossSeconds);
	e.m_previewVolume = std::clamp(options->previewVolume, 0, 100);
	e.m_previewMuted = options->previewMuted != 0;
}

WINDV_API void WINDV_CALL windv_get_status(windv_handle engine, windv_status* status)
{
	if (engine && status) {
		engine->GetStatus(status);
	}
}

WINDV_API std::int32_t WINDV_CALL windv_take_error(windv_handle engine, wchar_t* buffer, std::int32_t length)
{
	if (!engine) {
		return 0;
	}
	return CopyOut(engine->TakeError(), buffer, length);
}

WINDV_API windv_result WINDV_CALL windv_reset(windv_handle engine)
{
	if (!engine) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([](DVEngine& e) { e.Destroy(); });
}

WINDV_API windv_result WINDV_CALL windv_build_capture(windv_handle engine, const wchar_t* device)
{
	if (!engine || !device) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([&](DVEngine& e) { e.BuildCapturing(device); });
}

WINDV_API windv_result WINDV_CALL windv_capture_start(windv_handle engine, const wchar_t* fileBase,
                                                      const wchar_t* dateFormat, std::int32_t suffixDigits,
                                                      std::int64_t duration)
{
	if (!engine || !fileBase || !*fileBase) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([&](DVEngine& e) {
		if (e.GetState() != DVEngine::CapturePaused) {
			throw DShowError(L"The camcorder is not ready for capture");
		}
		e.StartCapturing(fileBase, Arg(dateFormat), std::clamp(suffixDigits, 0, 4),
		                 (std::max)(duration, std::int64_t{0}));
	});
}

WINDV_API windv_result WINDV_CALL windv_capture_stop(windv_handle engine)
{
	if (!engine) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([](DVEngine& e) { e.StopCapturing(); });
}

WINDV_API windv_result WINDV_CALL windv_transport(windv_handle engine, std::int32_t command)
{
	if (!engine || command < WINDV_DECK_PLAY || command > WINDV_DECK_REWIND) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([&](DVEngine& e) { e.Transport(static_cast<windv::DeckCommand>(command)); });
}

WINDV_API windv_result WINDV_CALL windv_build_record(windv_handle engine, const wchar_t* files, const wchar_t* device)
{
	if (!engine || !files || !device) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([&](DVEngine& e) { e.BuildRecording(files, device); });
}

WINDV_API windv_result WINDV_CALL windv_record_start(windv_handle engine)
{
	if (!engine) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([](DVEngine& e) { e.StartRecording(); });
}

WINDV_API windv_result WINDV_CALL windv_record_stop(windv_handle engine)
{
	if (!engine) {
		return WINDV_INVALID_ARGUMENT;
	}
	return engine->Call([](DVEngine& e) { e.StopRecording(); });
}

WINDV_API windv_result WINDV_CALL windv_parse_command_line(const wchar_t* const* args, std::int32_t count,
                                                           windv_command_line* result, wchar_t* files,
                                                           std::int32_t length, std::int32_t* needed)
{
	if (!result || !needed || count < 0 || (count > 0 && !args)) {
		return WINDV_INVALID_ARGUMENT;
	}

	std::vector<std::wstring> argv;
	for (std::int32_t i = 0; i < count; ++i) {
		argv.push_back(Arg(args[i]));
	}

	const auto parsed = windv::ParseCommandLine(argv);
	if (!parsed) {
		return WINDV_USAGE_ERROR;
	}

	result->mode = static_cast<std::int32_t>(parsed->mode);
	result->exitOnFinish = parsed->exitOnFinish ? 1 : 0;
	result->duration = parsed->duration;

	std::wstring text = parsed->captureFile;
	for (const std::wstring& file : parsed->recordFiles) {
		if (!text.empty()) {
			text += L" | ";
		}
		text += file;
	}
	*needed = CopyOut(text, files, length);
	return *needed > length ? WINDV_BUFFER_TOO_SMALL : WINDV_OK;
}

WINDV_API std::int32_t WINDV_CALL windv_capture_base(const wchar_t* filename, wchar_t* buffer, std::int32_t length)
{
	return CopyOut(windv::CaptureBaseFromFilename(Arg(filename)), buffer, length);
}

WINDV_API std::int32_t WINDV_CALL windv_is_valid_time_format(const wchar_t* format)
{
	return windv::IsValidTimeFormat(Arg(format)) ? 1 : 0;
}

WINDV_API std::int32_t WINDV_CALL windv_format_now(const wchar_t* format, wchar_t* buffer, std::int32_t length)
{
	return CopyOut(windv::FormatTime(Arg(format), std::time(nullptr)), buffer, length);
}
