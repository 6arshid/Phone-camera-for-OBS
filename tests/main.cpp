#include "test_framework.h"

#include <cstdio>
#include <vector>

namespace ocp_test {

namespace {
std::vector<TestCase> &registry()
{
	static std::vector<TestCase> tests;
	return tests;
}
int failures = 0;
}

bool registerTest(const std::string &name, std::function<void()> fn)
{
	registry().push_back(TestCase{name, std::move(fn)});
	return true;
}

void check(bool condition, const char *expr, const char *file, int line, const std::string &detail)
{
	if (condition)
		return;
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s:%d: CHECK failed: %s%s", file, line, expr, detail.c_str());
	throw Failure(buf);
}

int runAll()
{
	int failed = 0;
	int passed = 0;
	for (const auto &test : registry()) {
		try {
			test.fn();
			passed++;
			printf("[ OK ] %s\n", test.name.c_str());
		} catch (const Failure &f) {
			failed++;
			printf("[FAIL] %s\n       %s\n", test.name.c_str(), f.what());
		} catch (const std::exception &e) {
			failed++;
			printf("[FAIL] %s\n       unexpected exception: %s\n", test.name.c_str(),
			       e.what());
		}
	}
	printf("\n%d passed, %d failed, %zu total\n", passed, failed, registry().size());
	return failed;
}

}

int main()
{
	return ocp_test::runAll() == 0 ? 0 : 1;
}
