#include "json/json.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ocp {
namespace json {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
	Parser(std::string_view text) : text(text) {}

	Value parse(std::string *error)
	{
		skipWs();
		Value v = parseValue(0, error);
		if (error && !error->empty())
			return Value();
		skipWs();
		if (pos != text.size()) {
			if (error)
				*error = "trailing characters at offset " + std::to_string(pos);
			return Value();
		}
		return v;
	}

private:
	std::string_view text;
	size_t pos = 0;

	void fail(std::string *error, const std::string &msg)
	{
		if (error && error->empty())
			*error = msg + " at offset " + std::to_string(pos);
	}

	void skipWs()
	{
		while (pos < text.size()) {
			char c = text[pos];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				pos++;
			else
				break;
		}
	}

	bool consume(char c)
	{
		if (pos < text.size() && text[pos] == c) {
			pos++;
			return true;
		}
		return false;
	}

	bool literal(std::string_view lit)
	{
		if (text.substr(pos, lit.size()) == lit) {
			pos += lit.size();
			return true;
		}
		return false;
	}

	Value parseValue(int depth, std::string *error)
	{
		if (depth > kMaxDepth) {
			fail(error, "max depth exceeded");
			return Value();
		}
		if (pos >= text.size()) {
			fail(error, "unexpected end of input");
			return Value();
		}
		char c = text[pos];
		switch (c) {
		case '{':
			return parseObject(depth, error);
		case '[':
			return parseArray(depth, error);
		case '"': {
			std::string s;
			if (!parseString(s, error))
				return Value();
			return Value(std::move(s));
		}
		case 't':
			if (literal("true"))
				return Value(true);
			fail(error, "invalid literal");
			return Value();
		case 'f':
			if (literal("false"))
				return Value(false);
			fail(error, "invalid literal");
			return Value();
		case 'n':
			if (literal("null"))
				return Value(nullptr);
			fail(error, "invalid literal");
			return Value();
		default:
			return parseNumber(error);
		}
	}

	Value parseObject(int depth, std::string *error)
	{
		pos++;
		Object obj;
		skipWs();
		if (consume('}'))
			return Value(std::move(obj));
		while (true) {
			skipWs();
			if (pos >= text.size() || text[pos] != '"') {
				fail(error, "expected string key");
				return Value();
			}
			std::string key;
			if (!parseString(key, error))
				return Value();
			skipWs();
			if (!consume(':')) {
				fail(error, "expected ':'");
				return Value();
			}
			skipWs();
			Value v = parseValue(depth + 1, error);
			if (error && !error->empty())
				return Value();
			obj[std::move(key)] = std::move(v);
			skipWs();
			if (consume(','))
				continue;
			if (consume('}'))
				return Value(std::move(obj));
			fail(error, "expected ',' or '}'");
			return Value();
		}
	}

	Value parseArray(int depth, std::string *error)
	{
		pos++;
		Array arr;
		skipWs();
		if (consume(']'))
			return Value(std::move(arr));
		while (true) {
			skipWs();
			Value v = parseValue(depth + 1, error);
			if (error && !error->empty())
				return Value();
			arr.push_back(std::move(v));
			skipWs();
			if (consume(','))
				continue;
			if (consume(']'))
				return Value(std::move(arr));
			fail(error, "expected ',' or ']'");
			return Value();
		}
	}

	void appendUtf8(std::string &out, uint32_t cp)
	{
		if (cp < 0x80) {
			out.push_back(char(cp));
		} else if (cp < 0x800) {
			out.push_back(char(0xc0 | (cp >> 6)));
			out.push_back(char(0x80 | (cp & 0x3f)));
		} else if (cp < 0x10000) {
			out.push_back(char(0xe0 | (cp >> 12)));
			out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back(char(0x80 | (cp & 0x3f)));
		} else {
			out.push_back(char(0xf0 | (cp >> 18)));
			out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
			out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back(char(0x80 | (cp & 0x3f)));
		}
	}

	bool parseHex4(uint32_t &out, std::string *error)
	{
		if (pos + 4 > text.size()) {
			fail(error, "truncated \\u escape");
			return false;
		}
		out = 0;
		for (int i = 0; i < 4; i++) {
			char c = text[pos + i];
			uint32_t nibble;
			if (c >= '0' && c <= '9')
				nibble = uint32_t(c - '0');
			else if (c >= 'a' && c <= 'f')
				nibble = uint32_t(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				nibble = uint32_t(c - 'A' + 10);
			else {
				fail(error, "invalid \\u escape");
				return false;
			}
			out = (out << 4) | nibble;
		}
		pos += 4;
		return true;
	}

	bool parseString(std::string &out, std::string *error)
	{
		pos++;
		out.clear();
		while (true) {
			if (pos >= text.size()) {
				fail(error, "unterminated string");
				return false;
			}
			char c = text[pos++];
			if (c == '"')
				return true;
			if (c == '\\') {
				if (pos >= text.size()) {
					fail(error, "truncated escape");
					return false;
				}
				char e = text[pos++];
				switch (e) {
				case '"':
					out.push_back('"');
					break;
				case '\\':
					out.push_back('\\');
					break;
				case '/':
					out.push_back('/');
					break;
				case 'b':
					out.push_back('\b');
					break;
				case 'f':
					out.push_back('\f');
					break;
				case 'n':
					out.push_back('\n');
					break;
				case 'r':
					out.push_back('\r');
					break;
				case 't':
					out.push_back('\t');
					break;
				case 'u': {
					uint32_t cp;
					if (!parseHex4(cp, error))
						return false;
					if (cp >= 0xd800 && cp <= 0xdbff) {
						if (pos + 1 < text.size() && text[pos] == '\\' &&
						    text[pos + 1] == 'u') {
							size_t save = pos;
							pos += 2;
							uint32_t low;
							if (!parseHex4(low, error))
								return false;
							if (low >= 0xdc00 && low <= 0xdfff) {
								cp = 0x10000 +
								     ((cp - 0xd800) << 10) +
								     (low - 0xdc00);
								appendUtf8(out, cp);
							} else {
								pos = save;
								appendUtf8(out, 0xfffd);
							}
						} else {
							appendUtf8(out, 0xfffd);
						}
					} else if (cp >= 0xdc00 && cp <= 0xdfff) {
						appendUtf8(out, 0xfffd);
					} else {
						appendUtf8(out, cp);
					}
					break;
				}
				default:
					fail(error, "invalid escape");
					return false;
				}
			} else if ((unsigned char)c < 0x20) {
				fail(error, "unescaped control character");
				return false;
			} else {
				out.push_back(c);
			}
		}
	}

	Value parseNumber(std::string *error)
	{
		size_t start = pos;
		if (pos < text.size() && text[pos] == '-')
			pos++;
		bool digits = false;
		while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
			pos++;
			digits = true;
		}
		if (pos < text.size() && text[pos] == '.') {
			pos++;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
				pos++;
				digits = true;
			}
		}
		if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
			pos++;
			if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
				pos++;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				pos++;
		}
		if (!digits) {
			fail(error, "invalid number");
			return Value();
		}
		std::string numStr(text.substr(start, pos - start));
		return Value(std::strtod(numStr.c_str(), nullptr));
	}
};

void dumpString(const std::string &s, std::string &out)
{
	out.push_back('"');
	for (char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if ((unsigned char)c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
				out += buf;
			} else {
				out.push_back(c);
			}
		}
	}
	out.push_back('"');
}

void dumpNumber(double v, std::string &out)
{
	if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 9.007199254740992e15) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%lld", (long long)v);
		out += buf;
	} else if (std::isfinite(v)) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%.17g", v);
		out += buf;
	} else {
		out += "null";
	}
}

void dumpValue(const Value &v, std::string &out)
{
	switch (v.kind()) {
	case Type::Null:
		out += "null";
		break;
	case Type::Bool:
		out += v.asBool() ? "true" : "false";
		break;
	case Type::Number:
		dumpNumber(v.asNumber(), out);
		break;
	case Type::String:
		dumpString(v.asString(), out);
		break;
	case Type::Array: {
		out.push_back('[');
		bool first = true;
		for (const auto &item : v.items()) {
			if (!first)
				out.push_back(',');
			first = false;
			dumpValue(item, out);
		}
		out.push_back(']');
		break;
	}
	case Type::Object: {
		out.push_back('{');
		bool first = true;
		for (const auto &kv : v.members()) {
			if (!first)
				out.push_back(',');
			first = false;
			dumpString(kv.first, out);
			out.push_back(':');
			dumpValue(kv.second, out);
		}
		out.push_back('}');
		break;
	}
	}
}

}

bool Value::operator==(const Value &other) const
{
	if (type != other.type)
		return false;
	switch (type) {
	case Type::Null:
		return true;
	case Type::Bool:
		return boolVal == other.boolVal;
	case Type::Number:
		return numVal == other.numVal;
	case Type::String:
		return strVal == other.strVal;
	case Type::Array:
		return *arrVal == *other.arrVal;
	case Type::Object:
		return *objVal == *other.objVal;
	}
	return false;
}

std::string Value::dump() const
{
	std::string out;
	dumpValue(*this, out);
	return out;
}

Value Value::parse(std::string_view text, std::string *error)
{
	std::string localError;
	if (!error)
		error = &localError;
	error->clear();
	Parser p(text);
	Value v = p.parse(error);
	if (!error->empty())
		return Value();
	return v;
}

}
}
