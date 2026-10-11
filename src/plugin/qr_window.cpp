#include "qr_window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "qrcodegen.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace ocp {
namespace plugin {

namespace {

constexpr int kWindowW = 560;
constexpr int kWindowH = 640;

struct QrContent {
	std::wstring url;
	std::wstring code;
	std::wstring addresses;
	std::optional<qrcodegen::QrCode> qr;
	bool valid = false;
};

std::mutex g_mutex;
QrContent g_content;
HWND g_hwnd = nullptr;
std::thread g_thread;
bool g_threadStarted = false;

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

void paint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	RECT client;
	GetClientRect(hwnd, &client);

	HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
	FillRect(dc, &client, bg);
	DeleteObject(bg);

	std::wstring code, addresses;
	std::optional<qrcodegen::QrCode> qr;
	bool valid = false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		code = g_content.code;
		addresses = g_content.addresses;
		qr = g_content.qr;
		valid = g_content.valid;
	}

	int top = 24;
	if (valid && qr) {
		int size = qr->getSize();
		int qrSize = client.right - 80;
		if (qrSize > 420)
			qrSize = 420;
		int scale = qrSize / size;
		int dim = scale * size;
		int ox = (client.right - dim) / 2;

		HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
		RECT qrRect = {ox - 8, top - 8, ox + dim + 8, top + dim + 8};
		HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
		FillRect(dc, &qrRect, white);
		DeleteObject(white);
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				if (!qr->getModule(x, y))
					continue;
				RECT r = {ox + x * scale, top + y * scale,
					  ox + (x + 1) * scale, top + (y + 1) * scale};
				FillRect(dc, &r, black);
			}
		}
		DeleteObject(black);
		top += dim + 32;
	} else {
		RECT r = {0, client.bottom / 2 - 20, client.right, client.bottom / 2 + 20};
		DrawTextW(dc, L"QR unavailable", -1, &r, DT_CENTER | DT_SINGLELINE);
		top = client.bottom / 2 + 40;
	}

	SetTextColor(dc, RGB(20, 20, 20));
	SetBkMode(dc, TRANSPARENT);
	RECT codeRect = {20, top, client.right - 20, top + 36};
	DrawTextW(dc, code.c_str(), -1, &codeRect, DT_CENTER | DT_SINGLELINE);
	RECT addrRect = {20, top + 40, client.right - 20, top + 80};
	DrawTextW(dc, addresses.c_str(), -1, &addrRect, DT_CENTER | DT_WORDBREAK);

	EndPaint(hwnd, &ps);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_PAINT:
		paint(hwnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

void runWindowLoop()
{
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = wndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = L"ObsPhoneCameraPairingQr";
	RegisterClassExW(&wc);

	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Phone Camera – Pairing",
				    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
				    CW_USEDEFAULT, CW_USEDEFAULT, kWindowW, kWindowH, nullptr,
				    nullptr, wc.hInstance, nullptr);
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_hwnd = hwnd;
	}
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	g_hwnd = nullptr;
}

}

void showPairingQrWindow(const std::string &url, const std::string &code,
			 const std::string &addresses)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_content.url = utf8ToWide(url);
		g_content.code = utf8ToWide(code);
		g_content.addresses = utf8ToWide(addresses);
		try {
			g_content.qr = qrcodegen::QrCode::encodeText(url.c_str(),
								     qrcodegen::QrCode::Ecc::MEDIUM);
			g_content.valid = true;
		} catch (...) {
			g_content.valid = false;
		}
	}

	if (!g_threadStarted) {
		g_threadStarted = true;
		g_thread = std::thread(runWindowLoop);
	} else {
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_hwnd) {
			InvalidateRect(g_hwnd, nullptr, TRUE);
			SetForegroundWindow(g_hwnd);
		}
	}
}

void closePairingQrWindow()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_hwnd)
			PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
	}
	if (g_thread.joinable() && g_thread.get_id() != std::this_thread::get_id()) {
		g_thread.join();
		std::lock_guard<std::mutex> lock(g_mutex);
		g_threadStarted = false;
		g_hwnd = nullptr;
	}
}

}
}
