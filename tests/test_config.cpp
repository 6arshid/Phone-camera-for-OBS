#include "test_framework.h"

#include "settings/config.h"

using namespace ocp;

TEST(config_video_presets)
{
	VideoProfile p = resolveVideoProfile("1080p", 0, 0, "30", 0, 2, "auto");
	CHECK_EQ(p.width, 1920);
	CHECK_EQ(p.height, 1080);
	CHECK_EQ(p.fps, 30);
	CHECK(p.bitrateKbps > 0);

	p = resolveVideoProfile("720p", 0, 0, "60", 0, 2, "portrait");
	CHECK_EQ(p.width, 1280);
	CHECK_EQ(p.height, 720);
	CHECK_EQ(p.fps, 60);
	CHECK_STR_EQ(p.orientation, "portrait");
	CHECK(p.bitrateKbps > 0);

	p = resolveVideoProfile("custom", 640, 360, "24", 1500, 4, "auto");
	CHECK_EQ(p.width, 640);
	CHECK_EQ(p.height, 360);
	CHECK_EQ(p.fps, 24);
	CHECK_EQ(p.bitrateKbps, 1500);
	CHECK_EQ(p.gopS, 4);

	p = resolveVideoProfile("auto", 0, 0, "auto", 0, 2, "auto");
	CHECK_EQ(p.width, 0);
	CHECK_EQ(p.fps, 0);
	CHECK_EQ(p.bitrateKbps, 0);

	p = resolveVideoProfile("bogus", 0, 0, "bogus", -5, 99, "");
	CHECK_EQ(p.width, 1920);
	CHECK_EQ(p.fps, 30);
	CHECK_EQ(p.gopS, 10);
	CHECK_STR_EQ(p.orientation, "auto");
}

TEST(config_auto_bitrate)
{
	int b720 = autoBitrateKbps(1280, 720, 30);
	int b1080 = autoBitrateKbps(1920, 1080, 30);
	int b4k = autoBitrateKbps(3840, 2160, 30);
	int b1080_60 = autoBitrateKbps(1920, 1080, 60);
	CHECK(b720 > 0);
	CHECK(b1080 > b720);
	CHECK(b4k > b1080);
	CHECK(b1080_60 > b1080);
	CHECK(b4k <= 50000);
}

TEST(config_center_crop)
{
	CropRect r = centerCrop(1920, 1080, "16:9");
	CHECK_EQ(r.x, 0);
	CHECK_EQ(r.y, 0);
	CHECK_EQ(r.w, 1920);
	CHECK_EQ(r.h, 1080);

	r = centerCrop(1440, 1080, "16:9");
	CHECK_EQ(r.w, 1440);
	CHECK(r.h < 1080);
	CHECK_EQ(r.h, 810);
	CHECK_EQ(r.h % 2, 0);
	CHECK_EQ(r.y % 2, 0);

	r = centerCrop(1920, 1080, "9:16");
	CHECK_EQ(r.w, 608);
	CHECK_EQ(r.h, 1080);
	CHECK_EQ(r.x % 2, 0);

	r = centerCrop(1920, 1080, "1:1");
	CHECK_EQ(r.w, r.h);
	CHECK_EQ(r.w, 1080);

	r = centerCrop(1920, 1080, "source");
	CHECK_EQ(r.w, 1920);
	CHECK_EQ(r.h, 1080);
	CHECK_EQ(r.x, 0);
	CHECK_EQ(r.y, 0);
}
