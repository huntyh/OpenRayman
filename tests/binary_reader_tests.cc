#include "test_util.h"

#include <openrayman/binary_reader.h>

#include <doctest/doctest.h>

using namespace openrayman;

TEST_CASE("binary_reader reads little-endian values")
{
	bytes b;
	b.u8(0x12).u16(0x3456).u32(0x789ABCDE).f32(1.5f).str("abc").u8(0).u8('x');
	binary_reader r(b.data);

	CHECK(r.u8() == 0x12);
	CHECK(r.u16() == 0x3456);
	CHECK(r.u32() == 0x789ABCDE);
	CHECK(r.f32() == 1.5f);
	CHECK(r.string(5) == "abc"); // cut at the NUL, but consumes all 5 bytes
	CHECK(r.remaining() == 0);
	CHECK(r.at_end());
}

TEST_CASE("binary_reader throws instead of reading past the end")
{
	bytes b;
	b.u16(1);
	binary_reader r(b.data);

	CHECK_THROWS_AS(r.u32(), format_error);
	CHECK(r.position() == 0); // a failed read doesn't move
	CHECK_THROWS_AS(r.seek(3), format_error);
	CHECK_THROWS_AS(r.bytes(3), format_error);
	CHECK(r.u16() == 1);
}
