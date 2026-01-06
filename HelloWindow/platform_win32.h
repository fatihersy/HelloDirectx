
#pragma once

#include "dxgi1_6.h"

class platform
{
public:
	platform() { 
		//this->nCmdShow = 0;
		//->m_hwnd = nullptr;
		//this->hInstance = nullptr;
	};
	platform(UINT width, UINT height, std::wstring title, HINSTANCE hInstance, int nCmdShow);

	static void PlatShowWindow();
	static int PlatMessageDispatch(MSG& msg);

	static int GetCmdShow() { return nCmdShow; }
	static HWND GetHwnd() { return m_hwnd; }
	static HINSTANCE GethInstance() { return hInstance; }

	void SetCmdShow(int Cmd) { nCmdShow = Cmd; }
	void SetHwnd(HWND window) { m_hwnd = window; }
	void SethInstance(HINSTANCE instance) { hInstance = instance; }

protected:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	static int nCmdShow;
	static HWND m_hwnd;
	static HINSTANCE hInstance;
};
