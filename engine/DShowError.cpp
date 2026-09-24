// DShowError.cpp : the exception thrown by the DirectShow engine

#include "DShowBase.h"
#include "DShowError.h"

namespace {

std::wstring FormatMessageWithResult(const std::wstring& message, HRESULT hr)
{
	if (hr == S_OK)
		return message;

	WCHAR text[MAX_ERROR_TEXT_LEN] = L"";
	AMGetErrorTextW(hr, text, MAX_ERROR_TEXT_LEN);
	std::wstring description(text);
	while (!description.empty() && iswspace(description.back()))
		description.pop_back();

	wchar_t code[16] = L"";
	swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr));
	if (description.empty())
		return message + L" (" + code + L")";
	return message + L" (" + code + L": " + description + L")";
}

} // namespace

DShowError::DShowError(const std::wstring& message, HRESULT hr, Cause cause)
    : DShowError(FormatMessageWithResult(message, hr), hr, cause, 0)
{}

DShowError::DShowError(std::wstring formatted, HRESULT hr, Cause cause, int)
    : std::runtime_error(Narrow(formatted)), m_message(std::move(formatted)), m_hr(hr), m_cause(cause)
{}

void CheckHR(HRESULT hr, const std::wstring& what)
{
	if (hr != S_OK)
		throw DShowError(what, hr);
}

void CheckSucceeded(HRESULT hr, const std::wstring& what)
{
	if (FAILED(hr))
		throw DShowError(what, hr);
}

std::string Narrow(std::wstring_view text)
{
	if (text.empty())
		return {};
	const int length =
	    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string result(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr,
	                    nullptr);
	return result;
}

std::wstring Widen(std::string_view text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
	return result;
}
