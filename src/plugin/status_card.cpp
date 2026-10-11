#include "status_card.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace ocp {
namespace plugin {

namespace {

std::wstring utf8ToWide(const std::string &in)
{
	if (in.empty())
		return std::wstring();
	int len = MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0);
	if (len <= 0)
		return std::wstring();
	std::wstring out((size_t)len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), &out[0], len);
	return out;
}

}

bool renderStatusCard(const StatusCardInput &in, int width, int height,
		      std::vector<uint8_t> *bgraOut, int *strideOut)
{
	if (!bgraOut || !strideOut || width <= 0 || height <= 0)
		return false;

	HDC screen = GetDC(nullptr);
	if (!screen)
		return false;

	HDC dc = CreateCompatibleDC(screen);
	ReleaseDC(nullptr, screen);
	if (!dc)
		return false;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp || !bits) {
		DeleteDC(dc);
		return false;
	}

	HGDIOBJ oldBmp = SelectObject(dc, bmp);

	HFONT titleFont = CreateFontW(-height / 12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
				      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				      ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	HFONT lineFont = CreateFontW(-height / 24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				     ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	HGDIOBJ oldFont = SelectObject(dc, titleFont);
	SetBkMode(dc, TRANSPARENT);

	RECT rect = {0, 0, width, height};
	HBRUSH bg = CreateSolidBrush(RGB(24, 26, 32));
	FillRect(dc, &rect, bg);
	DeleteObject(bg);

	RECT accent = {0, 0, 8, height};
	HBRUSH accentBrush = CreateSolidBrush(RGB(46, 160, 220));
	FillRect(dc, &accent, accentBrush);
	DeleteObject(accentBrush);

	int padX = 48;
	int y = 40;
	SetTextColor(dc, RGB(235, 238, 245));
	RECT titleRect = {padX, y, width - padX, y + height / 10};
	std::wstring title = utf8ToWide(in.title);
	DrawTextW(dc, title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
	y += height / 8;

	SelectObject(dc, lineFont);
	SetTextColor(dc, RGB(170, 178, 190));
	const std::string *lines[] = {&in.line1, &in.line2, &in.line3, &in.line4};
	for (const std::string *line : lines) {
		RECT r = {padX, y, width - padX, y + height / 16};
		if (!line->empty()) {
			std::wstring wide = utf8ToWide(*line);
			DrawTextW(dc, wide.c_str(), -1, &r,
				  DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
		}
		y += height / 16;
	}

	int stride = width * 4;
	bgraOut->resize((size_t)stride * (size_t)height);
	memcpy(bgraOut->data(), bits, bgraOut->size());
	*strideOut = stride;

	SelectObject(dc, oldFont);
	SelectObject(dc, oldBmp);
	if (titleFont)
		DeleteObject(titleFont);
	if (lineFont)
		DeleteObject(lineFont);
	DeleteObject(bmp);
	DeleteDC(dc);
	return true;
}

}
}
