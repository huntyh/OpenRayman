# Third party dependencies, fetched at configure time and pinned to exact versions.
include(FetchContent)

if(OPENRAYMAN_BUILD_ENGINE)
	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
	set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
	set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(SDL3
		URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz
		DOWNLOAD_EXTRACT_TIMESTAMP ON)
	FetchContent_MakeAvailable(SDL3)

	FetchContent_Declare(glm
		URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
		DOWNLOAD_EXTRACT_TIMESTAMP ON)
	FetchContent_MakeAvailable(glm)
endif()

if(OPENRAYMAN_BUILD_TESTS)
	FetchContent_Declare(doctest
		URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
		DOWNLOAD_EXTRACT_TIMESTAMP ON)
	FetchContent_MakeAvailable(doctest)
endif()
