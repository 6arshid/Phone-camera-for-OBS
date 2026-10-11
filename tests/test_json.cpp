#include "test_framework.h"

#include "json/json.h"

using ocp::json::Value;

TEST(json_parse_basic)
{
	std::string err;
	Value v = Value::parse(R"({"a":1,"b":"two","c":true,"d":null,"e":[1,2,3]})", &err);
	CHECK(err.empty());
	CHECK(v.isObject());
	CHECK_EQ(v.getInt("a"), int64_t(1));
	CHECK_STR_EQ(v.getString("b"), "two");
	CHECK(v.getBool("c"));
	CHECK(v["d"].isNull());
	CHECK(v["e"].isArray());
	CHECK_EQ(v["e"].size(), size_t(3));
	CHECK_EQ(v["e"].items()[1].asInt(), int64_t(2));
}

TEST(json_round_trip)
{
	Value root = Value::object();
	root.set("name", "OBS \u062a\u06a9\u0647");
	root.set("pi", 3.5);
	root.set("count", int64_t(-42));
	root.set("on", true);
	root.set("nothing", Value());
	Value arr = Value::array();
	arr.push(1);
	arr.push("x");
	root.set("list", arr);

	std::string dumped = root.dump();
	std::string err;
	Value back = Value::parse(dumped, &err);
	CHECK(err.empty());
	CHECK(back == root);
}

TEST(json_string_escapes)
{
	std::string err;
	Value v = Value::parse(R"({"s":"line\nbreak \"q\" A"})", &err);
	CHECK(err.empty());
	CHECK_STR_EQ(v.getString("s"), "line\nbreak \"q\" A");

	std::string text = v.dump();
	Value v2 = Value::parse(text, &err);
	CHECK(err.empty());
	CHECK_STR_EQ(v2.getString("s"), v.getString("s"));
}

TEST(json_unicode_escapes)
{
	std::string err;
	Value v = Value::parse(R"({"u":"\u062a\u06a9\u0645"})" , &err);
	CHECK(err.empty());
	CHECK_STR_EQ(v.getString("u"), "\u062a\u06a9\u0645");

	std::string surrogate = R"({"u":"\ud83d\ude00"})";
	Value v2 = Value::parse(surrogate, &err);
	CHECK(err.empty());
	CHECK_STR_EQ(v2.getString("u"), "\U0001f600");
}

TEST(json_error_cases)
{
	struct Case {
		const char *text;
		bool shouldFail;
	};
	const Case cases[] = {
	    {"", true},
	    {"{", true},
	    {"[1,]", true},
	    {"{\"a\" 1}", true},
	    {"tru", true},
	    {"123", false},
	    {"  {\"ok\": true}  ", false},
	    {"{\"a\":1} garbage", true},
	};
	for (const auto &c : cases) {
		std::string err;
		Value v = Value::parse(c.text, &err);
		if (c.shouldFail) {
			CHECK(!err.empty());
		} else {
			CHECK(err.empty());
			CHECK(v.isObject() || v.isNumber());
		}
	}
}

TEST(json_number_formatting)
{
	Value v = Value::object();
	v.set("i", int64_t(7));
	v.set("d", 1.5);
	CHECK_STR_EQ(v.dump(), R"({"d":1.5,"i":7})");
}
