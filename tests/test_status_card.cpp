#include "test_framework.h"
#include "status_card.h"
#include <windows.h>
#include <fstream>

TEST(status_card_has_opaque_pairing_qr)
{
    ocp::plugin::StatusCardInput input;
    input.title = "Phone Camera";
    input.line1 = "Scan this QR with the phone app";
    input.pairingUrl = "ocp1://pair?h=192.168.1.10&p=47100&s=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&n=OBS";
    std::vector<uint8_t> pixels;
    int stride = 0;
    CHECK(ocp::plugin::renderStatusCard(input, 1280, 720, &pixels, &stride));
    CHECK_EQ(stride, 5120);
    int whites = 0, blacks = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        CHECK_EQ((int)pixels[i + 3], 255);
        if (pixels[i] == 255 && pixels[i + 1] == 255 && pixels[i + 2] == 255) ++whites;
        if (pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 0) ++blacks;
    }
    CHECK(whites > 10000 && blacks > 10000);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + (DWORD)pixels.size();
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = 1280;
    info.biHeight = -720;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::ofstream file("pairing-card.bmp", std::ios::binary);
    file.write((const char *)&header, sizeof(header));
    file.write((const char *)&info, sizeof(info));
    file.write((const char *)pixels.data(), pixels.size());
    CHECK(file.good());
}
