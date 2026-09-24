// ComApartment.h : joins the COM multithreaded apartment for a scope
#pragma once

#include "DShowBase.h"

// Initializes COM on a worker thread for as long as the object lives.
class ComApartment {
public:
	ComApartment() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
	~ComApartment()
	{
		if (SUCCEEDED(m_hr))
			CoUninitialize();
	}
	ComApartment(const ComApartment&) = delete;
	ComApartment& operator=(const ComApartment&) = delete;

private:
	HRESULT m_hr;
};
