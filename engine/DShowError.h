// DShowError.h : the exception thrown by the DirectShow engine
#pragma once

#include "DShowBase.h"

class DShowError : public std::runtime_error {
public:
	enum class Cause { Error, DeviceNotFound };

	explicit DShowError(const std::wstring& message, HRESULT hr = S_OK, Cause cause = Cause::Error);

	// The user-facing message, including the HRESULT and its description.
	const std::wstring& Message() const { return m_message; }
	HRESULT Result() const { return m_hr; }
	Cause GetCause() const { return m_cause; }

private:
	DShowError(std::wstring formatted, HRESULT hr, Cause cause, int);

	std::wstring m_message;
	HRESULT m_hr;
	Cause m_cause;
};

// Throws DShowError unless hr is exactly S_OK.
void CheckHR(HRESULT hr, const std::wstring& what);
// Throws DShowError if hr is a failure code; success codes such as S_FALSE pass.
void CheckSucceeded(HRESULT hr, const std::wstring& what);

// UTF-16 <-> UTF-8 for std::exception::what() and back.
std::string Narrow(std::wstring_view text);
std::wstring Widen(std::string_view text);
