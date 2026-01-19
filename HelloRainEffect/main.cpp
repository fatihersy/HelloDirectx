
#include "stdafx.h"
#include "app.h"

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	app app(1280, 720, L"Hello Rain Effect", hInstance, nCmdShow);

	app.OnInit();

	app.Run();

	return 0;
}
