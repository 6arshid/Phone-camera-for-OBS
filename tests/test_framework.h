#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ocp_test {

struct TestCase {
	std::string name;
	std::function<void()> fn;
};

bool registerTest(const std::string &name, std::function<void()> fn);

struct Failure : std::runtime_error {
	explicit Failure(const std::string &what) : std::runtime_error(what) {}
};

void check(bool condition, const char *expr, const char *file, int line,
	   const std::string &detail = std::string());

int runAll();

template<typename A, typename B>
std::string formatValues(const A &a, const B &b)
{
	std::ostringstream os;
	os << " (actual=" << a << ", expected=" << b << ")";
	return os.str();
}

}

#define TEST(name)                                                             \
	static void ocp_test_##name();                                         \
	static bool ocp_test_reg_##name = ::ocp_test::registerTest(#name, ocp_test_##name); \
	static void ocp_test_##name()

#define CHECK(cond) ::ocp_test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                        \
	do {                                                                  \
		auto ocp_a = (a);                                             \
		auto ocp_b = (b);                                             \
		if (!(ocp_a == ocp_b))                                        \
			::ocp_test::check(false, #a " == " #b, __FILE__,      \
					  __LINE__,                            \
					  ::ocp_test::formatValues(ocp_a, ocp_b)); \
	} while (0)

#define CHECK_STR_EQ(a, b) CHECK_EQ(std::string(a), std::string(b))
