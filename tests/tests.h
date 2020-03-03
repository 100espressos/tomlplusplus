#pragma once
#define TOML_UNDEF_MACROS 0
#define TOML_ALL_INLINE 0
#include "../include/toml++/toml.h"
TOML_PUSH_WARNINGS
TOML_DISABLE_ALL_WARNINGS
#include "catch2.h"
#include <sstream>
using namespace toml;
using namespace Catch::literals;
TOML_POP_WARNINGS

#define S(str) TOML_STRING_PREFIX(str)

template <typename CHAR, typename FUNC>
inline void parsing_should_succeed(std::basic_string_view<CHAR> toml_str, FUNC&& func, std::string_view source_path = {}) noexcept
{
	INFO("String being parsed: '"sv << std::string_view( reinterpret_cast<const char*>(toml_str.data()), toml_str.length() ) << "'"sv)

	constexpr auto validate_table = [](table&& tabl, std::string_view path) noexcept -> table&&
	{
		INFO("Validating table source information"sv)
		CHECK(tabl.source().begin != source_position{});
		CHECK(tabl.source().end != source_position{});
		if (path.empty())
			CHECK(tabl.source().path == nullptr);
		else
		{
			REQUIRE(tabl.source().path != nullptr);
			CHECK(*tabl.source().path == path);
		}
		return std::move(tabl);
	};

	#if TOML_EXCEPTIONS

	try
	{
		{
			INFO("Parsing string directly"sv)
			std::forward<FUNC>(func)(validate_table(toml::parse(toml_str, source_path), source_path));
		}
		{
			INFO("Parsing from a string stream"sv)
			std::basic_stringstream<CHAR, std::char_traits<CHAR>, std::allocator<CHAR>> ss;
			ss.write(toml_str.data(), static_cast<std::streamsize>(toml_str.length()));
			std::forward<FUNC>(func)(validate_table(toml::parse(ss, source_path), source_path));
		}
	}
	catch (const parse_error& err)
	{
		FAIL(
			"Parse error on line "sv << err.source().begin.line
			<< ", column "sv << err.source().begin.column
			<< ":\n"sv << err.description()
		);
	}

	#else

	{
		INFO("Parsing string directly"sv)
		parse_result result = toml::parse(toml_str, source_path);
		if (result)
			std::forward<FUNC>(func)(validate_table(std::move(result), source_path));
		else
		{
			FAIL(
				"Parse error on line "sv << result.error().source().begin.line
				<< ", column "sv << result.error().source().begin.column
				<< ":\n"sv << result.error().description()
			);
			return;
		}
	}

	{
		INFO("Parsing from a string stream"sv)
		std::basic_stringstream<CHAR, std::char_traits<CHAR>, std::allocator<CHAR>> ss;
		ss.write(toml_str.data(), static_cast<std::streamsize>(toml_str.length()));
		parse_result result = toml::parse(ss, source_path);
		if (result)
			std::forward<FUNC>(func)(validate_table(std::move(result), source_path));
		else
		{
			FAIL(
				"Parse error on line "sv << result.error().source().begin.line
				<< ", column "sv << result.error().source().begin.column
				<< ":\n"sv << result.error().description()
			);
			return;
		}
	}

	#endif
}

template <typename CHAR>
inline void parsing_should_fail(std::basic_string_view<CHAR> toml_str) noexcept
{
	#if TOML_EXCEPTIONS

	static constexpr auto run_tests = [](auto&& fn) noexcept
	{
		try
		{
			fn();
		}
		catch (const parse_error&)
		{
			SUCCEED("parse_error thrown OK"sv);
			return true;
		}
		catch (const std::exception& exc)
		{
			FAIL("Expected parsing failure, saw exception: "sv << exc.what());
		}
		catch (...)
		{
			FAIL("Expected parsing failure, saw unspecified error"sv);
		}
		return false;
	};

	if (run_tests([=]() { (void)toml::parse(toml_str); }))
		run_tests([=]()
		{
			std::basic_stringstream<CHAR, std::char_traits<CHAR>, std::allocator<CHAR>> ss;
			ss.write(toml_str.data(), static_cast<std::streamsize>(toml_str.length()));
			(void)toml::parse(ss);
		});

	#else

	static constexpr auto run_tests = [](auto&& fn) noexcept
	{
		parse_result result = fn();
		if (result)
		{
			FAIL("Expected parsing failure"sv);
			return false;
		}
		else
		{
			SUCCEED("parse_error returned OK"sv);
			return true;
		}
	};

	if (run_tests([=]() noexcept { return toml::parse(toml_str); }))
		run_tests([=]() noexcept
		{
			std::basic_stringstream<CHAR, std::char_traits<CHAR>, std::allocator<CHAR>> ss;
			ss.write(toml_str.data(), static_cast<std::streamsize>(toml_str.length()));
			return toml::parse(ss);
		});

	#endif
}

template <typename T>
inline void parse_expected_value(std::string_view value_str, const T& expected) noexcept
{
	std::string val;
	static constexpr auto key = "val = "sv;
	val.reserve(key.length() + value_str.length());
	val.append(key);
	val.append(value_str);

	static constexpr auto is_val = [](char32_t codepoint) noexcept
	{
		if constexpr (std::is_same_v<string, impl::promoted<T>>)
			return codepoint == U'"' || codepoint == U'\'';
		else
			return !impl::is_whitespace(codepoint);
	};

	source_position pos{ 1,  static_cast<source_index>(key.length()) };
	source_position begin{}, end{};
	impl::utf8_decoder decoder;
	for (auto c : value_str)
	{
		decoder(static_cast<uint8_t>(c));
		if (!decoder.has_code_point())
			continue;

		if (impl::is_line_break(decoder.codepoint))
		{
			if (decoder.codepoint != U'\r')
			{
				pos.line++;
				pos.column = source_index{ 1 };
			}
			continue;
		}

		pos.column++;
		if (is_val(decoder.codepoint))
		{
			if (!begin)
				begin = pos;
			else
				end = pos;
		}
	}
	if (!end)
		end = begin;
	end.column++;

	using value_type = impl::promoted<impl::remove_cvref_t<T>>;
	value<value_type> val_parsed;

	parsing_should_succeed(std::string_view{ val }, [&](table&& tbl) noexcept
	{
		CHECK(tbl.size() == 1);
		auto nv = tbl[S("val"sv)];
		REQUIRE(nv);
		REQUIRE(nv.as<value_type>());
		REQUIRE(nv.get()->type() == impl::node_type_of<T>);

		// check the raw value
		CHECK(nv.get()->value<value_type>() == expected);
		CHECK(nv.get()->value_or(T{}) == expected);
		CHECK(nv.as<value_type>()->get() == expected);
		CHECK(nv.value<value_type>() == expected);
		CHECK(nv.value_or(T{}) == expected);

		// check the table relops
		CHECK(tbl == table{ { { S("val"sv), expected } } });
		CHECK(!(tbl != table{ { { S("val"sv), expected } } }));

		// check the value relops
		CHECK(*nv.as<value_type>() == expected);
		CHECK(expected == *nv.as<value_type>());
		CHECK(!(*nv.as<value_type>() != expected));
		CHECK(!(expected != *nv.as<value_type>()));

		// check the node_view relops
		CHECK(nv == expected);
		CHECK(expected == nv);
		CHECK(!(nv != expected));
		CHECK(!(expected != nv));

		// make sure source info is correct
		CHECK(nv.get()->source().begin == begin);
		CHECK(nv.get()->source().end == end);

		// steal the val for round-trip tests
		val_parsed = std::move(*nv.as<value_type>());
	});

	// check round-tripping
	value<value_type> val_reparsed;
	{
		std::string str;
		{
			auto tbl = table{ { { S("val"sv), *val_parsed } } };
			std::stringstream ss;
			ss << tbl;
			str = ss.str();
		}

		parsing_should_succeed(std::string_view{ str }, [&](table&& tbl) noexcept
		{
			CHECK(tbl.size() == 1);
			auto nv = tbl[S("val"sv)];
			REQUIRE(nv);
			REQUIRE(nv.as<value_type>());
			REQUIRE(nv.get()->type() == impl::node_type_of<T>);

			CHECK(nv.as<value_type>()->get() == expected);
			CHECK(nv.value_or(T{}) == expected);

			val_reparsed = std::move(*nv.as<value_type>());
		});
	}
	CHECK(val_reparsed == val_parsed);
	CHECK(val_reparsed == expected);
}

// manually instantiate some templates to reduce test compilation time (chosen using ClangBuildAnalyzer)
#define TESTS_MANUAL_INSTANTIATIONS 1
#if TESTS_MANUAL_INSTANTIATIONS

extern template void parse_expected_value(std::string_view, const int&) noexcept;
extern template void parse_expected_value(std::string_view, const unsigned int&) noexcept;
extern template void parse_expected_value(std::string_view, const bool&) noexcept;
extern template void parse_expected_value(std::string_view, const float&) noexcept;
extern template void parse_expected_value(std::string_view, const double&) noexcept;
extern template void parse_expected_value(std::string_view, const toml::string_view&) noexcept;

TOML_IMPL_START
{
	extern template class formatter<char>;
}
TOML_IMPL_END

TOML_START
{
	extern template class default_formatter<char>;

	extern template std::ostream& operator<< (std::ostream&, const table&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const array&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<string>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<int64_t>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<double>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<bool>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<date>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<time>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const value<date_time>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const node_view<node>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const node_view<const node>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, node_type) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const source_region&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const source_position&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const parse_error&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const date&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const time&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const time_offset&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, const date_time&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, default_formatter<char>&) TOML_MAY_THROW;
	extern template std::ostream& operator<< (std::ostream&, default_formatter<char>&&) TOML_MAY_THROW;
}
TOML_END

extern template class std::unique_ptr<const Catch::IExceptionTranslator>;
namespace Catch
{
	extern template struct StringMaker<node_view<node>>;
	extern template struct StringMaker<node_view<const node>>;
	extern template ReusableStringStream& ReusableStringStream::operator << (node_view<node> const&);
	extern template ReusableStringStream& ReusableStringStream::operator << (node_view<const node> const&);
	namespace Detail
	{
		extern template std::string stringify(const node_view<node>&);
		extern template std::string stringify(const node_view<const node>&);
	}
}

#endif // TESTS_MANUAL_INSTANTIATIONS
