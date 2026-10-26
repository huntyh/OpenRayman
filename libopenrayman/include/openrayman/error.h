#pragma once

#include <stdexcept>

namespace openrayman
{
	// Thrown when game data is malformed or not in the expected format.
	class format_error : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};
}
