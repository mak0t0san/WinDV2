/* windv_api.h : flat C API over the DirectShow engine, for the WinUI front end
 *
 * Threading
 *   The engine runs on a thread of its own in the COM multithreaded apartment.
 *   Every call is forwarded to that thread and waits for it, so calls may come
 *   from any thread. Slow calls (building a pipeline opens the device) should
 *   not be made on a UI thread. windv_get_status and windv_set_options never
 *   wait and are safe to call from a UI timer.
 *
 *   windv_create and windv_preview_move must be called on the thread that owns
 *   the parent window: the preview is a child window of it.
 *
 *   The event callback runs on engine and worker threads. It must not call
 *   back into this API; post to the UI thread instead.
 *
 * Strings are UTF-16. Functions that fill a buffer return the length needed
 * including the terminating null; when that exceeds the buffer's length,
 * nothing is written.
 *
 * Exported undecorated (see WinDV.Native.def); WINDV_CALL is stdcall on x86.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
#define WINDV_EXTERN extern "C"
#else
#define WINDV_EXTERN
#endif

#ifdef WINDV_NATIVE_EXPORTS
#define WINDV_API WINDV_EXTERN /* exported by WinDV.Native.def */
#else
#define WINDV_API WINDV_EXTERN __declspec(dllimport)
#endif
#define WINDV_CALL __stdcall

typedef struct windv_engine* windv_handle;

typedef enum windv_result {
	WINDV_OK = 0,
	WINDV_FAILED = 1,           /* message available from windv_take_error */
	WINDV_DEVICE_NOT_FOUND = 2, /* also has a message */
	WINDV_INVALID_ARGUMENT = 3,
	WINDV_BUFFER_TOO_SMALL = 4,
	WINDV_USAGE_ERROR = 5, /* windv_parse_command_line: bad arguments */
} windv_result;

/* Same values as DVEngine::State. */
typedef enum windv_state {
	WINDV_IDLE = 0,
	WINDV_RECORD_PAUSED = 1,
	WINDV_RECORDING = 2,
	WINDV_CAPTURE_PAUSED = 3,
	WINDV_CAPTURING = 4,
	WINDV_FINISHED = 5,
} windv_state;

/* Same values as windv::DeckCommand. */
typedef enum windv_deck_command {
	WINDV_DECK_PLAY = 0,
	WINDV_DECK_PAUSE = 1,
	WINDV_DECK_STOP = 2,
	WINDV_DECK_FAST_FORWARD = 3,
	WINDV_DECK_REWIND = 4,
} windv_deck_command;

/* Same values as windv::DeckMode. */
typedef enum windv_deck_mode {
	WINDV_MODE_UNKNOWN = 0,
	WINDV_MODE_STOPPED = 1,
	WINDV_MODE_PLAYING = 2,
	WINDV_MODE_PAUSED = 3,
	WINDV_MODE_FAST_FORWARD = 4,
	WINDV_MODE_REWIND = 5,
	WINDV_MODE_CUE_FORWARD = 6,
	WINDV_MODE_CUE_REVERSE = 7,
	WINDV_MODE_RECORDING = 8,
	WINDV_MODE_RECORD_PAUSED = 9,
} windv_deck_mode;

typedef enum windv_event {
	WINDV_EVENT_DV_TIME_CHANGED = 1, /* status.dvTime changed */
	WINDV_EVENT_ERROR = 2,           /* a worker failed; call windv_take_error */
} windv_event;

typedef void(WINDV_CALL* windv_event_callback)(void* context, int32_t event);

typedef struct windv_status {
	int32_t state;          /* windv_state */
	int32_t deckMode;       /* windv_deck_mode */
	int32_t canControlDeck; /* the device accepts transport commands */
	int32_t dropped;        /* frames dropped by the device during this capture */
	int32_t counter;        /* frames captured / recorded; -1 when idle */
	int32_t queueLoad;      /* frames waiting between source and sink */
	int32_t queueCapacity;
	int32_t framesReceived; /* frames from the source since the pipeline was built */
	int32_t stopReason;     /* windv_stop_reason: why state became WINDV_FINISHED */
	int32_t reserved;
	int64_t time;   /* position in 100 ns units; -1 when idle */
	int64_t dvTime; /* camcorder recording time (time_t, local); 0 if unknown */
} windv_status;

typedef struct windv_options {
	int32_t type2AVI;               /* capture: type-2 AVI (separate audio stream) */
	int32_t discontinuityThreshold; /* capture: new file on a timestamp jump > N s; 0 = never */
	int32_t maxAVIFrames;           /* capture: new file after N frames */
	int32_t everyNth;               /* capture: keep every Nth frame */
	int32_t recordPreview;          /* record: show the picture */
	int32_t deckFollowsPipeline;    /* the deck plays/pauses/records with capture and record */
	int32_t signalLossSeconds;      /* capture: stop after N s without a DV signal; 0 = never */
} windv_options;

/* Same values as DVEngine::StopReason. */
typedef enum windv_stop_reason {
	WINDV_STOP_NONE = 0,
	WINDV_STOP_DURATION = 1,     /* capture reached the requested duration */
	WINDV_STOP_END_OF_FILES = 2, /* record: all files sent to tape */
	WINDV_STOP_SIGNAL_LOST = 3,  /* capture: no DV signal for signalLossSeconds */
	WINDV_STOP_DISK_FULL = 4,    /* capture: disk nearly full; the file was finished */
} windv_stop_reason;

typedef enum windv_command_mode {
	WINDV_COMMAND_INTERACTIVE = 0,
	WINDV_COMMAND_CAPTURE = 1,
	WINDV_COMMAND_RECORD = 2,
} windv_command_mode;

typedef struct windv_command_line {
	int32_t mode; /* windv_command_mode */
	int32_t exitOnFinish;
	int64_t duration; /* capture: 100 ns units, 0 = until stopped */
} windv_command_line;

/* Creates the engine and its preview window (hidden until windv_preview_move). */
WINDV_API windv_result WINDV_CALL windv_create(void* parentHwnd, windv_event_callback callback, void* context,
                                               windv_handle* engine);
/* Stops everything and frees the engine. Safe to call on the UI thread. */
WINDV_API void WINDV_CALL windv_destroy(windv_handle engine);

/* Video capture devices, separated by '\n'. */
WINDV_API windv_result WINDV_CALL windv_list_devices(windv_handle engine, wchar_t* buffer, int32_t length,
                                                     int32_t* needed);

/* Positions (in the parent's client pixels) and shows the preview; 0 size hides it. */
WINDV_API void WINDV_CALL windv_preview_move(windv_handle engine, int32_t x, int32_t y, int32_t width, int32_t height);

WINDV_API void WINDV_CALL windv_set_options(windv_handle engine, const windv_options* options);
WINDV_API void WINDV_CALL windv_get_status(windv_handle engine, windv_status* status);

/* The error from the last failed call, or else from a worker; then cleared. */
WINDV_API int32_t WINDV_CALL windv_take_error(windv_handle engine, wchar_t* buffer, int32_t length);

/* Stops and releases the pipeline and the device. */
WINDV_API windv_result WINDV_CALL windv_reset(windv_handle engine);

/* Capture from a camcorder: build (live preview), then start/stop writing files. */
WINDV_API windv_result WINDV_CALL windv_build_capture(windv_handle engine, const wchar_t* device);
WINDV_API windv_result WINDV_CALL windv_capture_start(windv_handle engine, const wchar_t* fileBase,
                                                      const wchar_t* dateFormat, int32_t suffixDigits,
                                                      int64_t duration);
WINDV_API windv_result WINDV_CALL windv_capture_stop(windv_handle engine);
WINDV_API windv_result WINDV_CALL windv_transport(windv_handle engine, int32_t command);

/* Record to tape: files is a '|'-separated list, entries may have wildcards. */
WINDV_API windv_result WINDV_CALL windv_build_record(windv_handle engine, const wchar_t* files, const wchar_t* device);
WINDV_API windv_result WINDV_CALL windv_record_start(windv_handle engine);
WINDV_API windv_result WINDV_CALL windv_record_stop(windv_handle engine);

/* Parses argv[1..] (see core/CommandLine.h). files receives the capture file,
   or the record files joined with " | ". */
WINDV_API windv_result WINDV_CALL windv_parse_command_line(const wchar_t* const* args, int32_t count,
                                                           windv_command_line* result, wchar_t* files, int32_t length,
                                                           int32_t* needed);

/* Capture base name from a filename ("D:\dv\tape.04-07-15.00.avi" -> "D:\dv\tape"). */
WINDV_API int32_t WINDV_CALL windv_capture_base(const wchar_t* filename, wchar_t* buffer, int32_t length);

/* Validates a strftime-style date format for capture file names. */
WINDV_API int32_t WINDV_CALL windv_is_valid_time_format(const wchar_t* format);

/* Formats the current local time as the capture file names would. */
WINDV_API int32_t WINDV_CALL windv_format_now(const wchar_t* format, wchar_t* buffer, int32_t length);
