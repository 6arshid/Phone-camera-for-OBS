#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ocp {
namespace json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
	Value() : type(Type::Null) {}
	Value(std::nullptr_t) : type(Type::Null) {}
	Value(bool b) : type(Type::Bool), boolVal(b) {}
	Value(double d) : type(Type::Number), numVal(d) {}
	Value(int i) : type(Type::Number), numVal(double(i)) {}
	Value(int64_t i) : type(Type::Number), numVal(double(i)) {}
	Value(uint32_t i) : type(Type::Number), numVal(double(i)) {}
	Value(uint64_t i) : type(Type::Number), numVal(double(i)) {}
	Value(const char *s) : type(Type::String), strVal(s ? s : "") {}
	Value(std::string s) : type(Type::String), strVal(std::move(s)) {}
	Value(Array a) : type(Type::Array), arrVal(std::make_shared<Array>(std::move(a))) {}
	Value(Object o) : type(Type::Object), objVal(std::make_shared<Object>(std::move(o))) {}

	static Value array() { return Value(Array{}); }
	static Value object() { return Value(Object{}); }

	Type kind() const { return type; }
	bool isNull() const { return type == Type::Null; }
	bool isBool() const { return type == Type::Bool; }
	bool isNumber() const { return type == Type::Number; }
	bool isString() const { return type == Type::String; }
	bool isArray() const { return type == Type::Array; }
	bool isObject() const { return type == Type::Object; }

	bool asBool(bool def = false) const { return isBool() ? boolVal : def; }
	double asNumber(double def = 0.0) const { return isNumber() ? numVal : def; }
	int64_t asInt(int64_t def = 0) const
	{
		return isNumber() ? int64_t(numVal < 0 ? numVal - 0.5 : numVal + 0.5) : def;
	}
	const std::string &asString() const
	{
		static const std::string empty;
		return isString() ? strVal : empty;
	}
	std::string asString(const std::string &def) const { return isString() ? strVal : def; }

	const Array &items() const
	{
		static const Array empty;
		return isArray() ? *arrVal : empty;
	}
	Array &items()
	{
		if (!isArray()) {
			type = Type::Array;
			arrVal = std::make_shared<Array>();
		}
		return *arrVal;
	}

	const Object &members() const
	{
		static const Object empty;
		return isObject() ? *objVal : empty;
	}
	Object &members()
	{
		if (!isObject()) {
			type = Type::Object;
			objVal = std::make_shared<Object>();
		}
		return *objVal;
	}

	bool has(std::string_view key) const
	{
		return isObject() && objVal->find(std::string(key)) != objVal->end();
	}

	const Value &operator[](std::string_view key) const
	{
		static const Value nullValue;
		if (!isObject())
			return nullValue;
		auto it = objVal->find(std::string(key));
		return it == objVal->end() ? nullValue : it->second;
	}

	Value &set(const std::string &key, Value v)
	{
		Object &o = members();
		o[key] = std::move(v);
		return *this;
	}

	Value get(std::string_view key, Value def) const
	{
		const Value &v = (*this)[key];
		return v.isNull() ? std::move(def) : v;
	}

	std::string getString(std::string_view key, const std::string &def = std::string()) const
	{
		const Value &v = (*this)[key];
		return v.isString() ? v.strVal : def;
	}
	int64_t getInt(std::string_view key, int64_t def = 0) const
	{
		return (*this)[key].asInt(def);
	}
	double getNumber(std::string_view key, double def = 0.0) const
	{
		return (*this)[key].asNumber(def);
	}
	bool getBool(std::string_view key, bool def = false) const
	{
		const Value &v = (*this)[key];
		return v.isBool() ? v.boolVal : def;
	}

	void push(Value v)
	{
		Array &a = items();
		a.push_back(std::move(v));
	}

	size_t size() const
	{
		if (isArray())
			return arrVal->size();
		if (isObject())
			return objVal->size();
		return 0;
	}

	bool operator==(const Value &other) const;
	bool operator!=(const Value &other) const { return !(*this == other); }

	std::string dump() const;

	static Value parse(std::string_view text, std::string *error = nullptr);

private:
	Type type;
	bool boolVal = false;
	double numVal = 0.0;
	std::string strVal;
	std::shared_ptr<Array> arrVal;
	std::shared_ptr<Object> objVal;
};

}

}
