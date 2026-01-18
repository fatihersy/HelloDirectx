
#pragma once

#include "dxgi1_6.h"
#include "IApp.h"

class platform
{
public:
	platform() {};
	platform(UINT width, UINT height, std::wstring title, HINSTANCE hInstance, int nCmdShow, IApp* iapp);

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
