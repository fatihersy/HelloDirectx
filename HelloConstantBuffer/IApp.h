

#pragma once

#include <dxgi1_6.h>

class IApp
{
public:
	IApp(UINT width, UINT height, std::wstring name);
	virtual ~IApp();

	virtual void OnInit() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnRender() = 0;
	virtual void OnDestroy() = 0;

	virtual void OnKeyDown(UINT8 key) = 0;
	virtual void OnKeyUp(UINT8 key) = 0;

	UINT m_width;
	UINT m_height;
	std::wstring m_title;
};
