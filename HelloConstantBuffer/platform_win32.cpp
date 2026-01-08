
#include "stdafx.h"
#include "platform_win32.h"

int platform::nCmdShow = 0;
HWND platform::m_hwnd = nullptr;
HINSTANCE platform::hInstance = nullptr;

platform::platform(UINT width, UINT height, std::wstring title, HINSTANCE hInstance, int nCmdShow, IApp* iapp) 
{
	platform::nCmdShow = nCmdShow;
	platform::hInstance = hInstance;

	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.lpszClassName = title.c_str();
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

	platform::m_hwnd = CreateWindow(
		windowClass.lpszClassName,
		title.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,		// We have no parent window.
		nullptr,		// We aren't using menus.
		hInstance,
		iapp
	);
}

void platform::PlatShowWindow()
{
	ShowWindow(platform::m_hwnd, platform::nCmdShow);
}
int platform::PlatMessageDispatch(MSG& msg)
{
	// Process any messages in the queue.
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return static_cast<char>(msg.wParam);
}

// Main message handler for the sample.
LRESULT CALLBACK platform::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	IApp* iapp = reinterpret_cast<IApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message)
	{
	case WM_CREATE:
	{
		LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
	}
	return 0;

	case WM_KEYDOWN:
		if (iapp)
		{
			iapp->OnKeyDown(static_cast<unsigned int>(wParam));
		}
		return 0;

	case WM_KEYUP:
		if (iapp)
		{
			iapp->OnKeyUp(static_cast<unsigned int>(wParam));
		}
		return 0;

	case WM_PAINT:
		if (iapp)
		{
			iapp->OnUpdate();
			iapp->OnRender();
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}
