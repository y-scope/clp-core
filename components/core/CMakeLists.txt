cmake_minimum_required(VERSION 3.16.3)
project(CLP LANGUAGES CXX C)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(default_build_type "Release")
    message(STATUS "No build type specified. Setting to '${default_build_type}'.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set general compressor
set(GENERAL_COMPRESSOR "zstd" CACHE STRING "The general-purpose compressor used as the 2nd-stage compressor")
set_property(CACHE GENERAL_COMPRESSOR PROPERTY STRINGS lzma passthrough zstd)
if ("${GENERAL_COMPRESSOR}" STREQUAL "lzma")
    add_definitions(-DUSE_LZMA_COMPRESSION=1)
    message(STATUS "Using Lempel–Ziv–Markov chain Algorithm compression")
elseif ("${GENERAL_COMPRESSOR}" STREQUAL "passthrough")
    add_definitions(-DUSE_PASSTHROUGH_COMPRESSION=1)
    message(STATUS "Using passthrough compression")
elseif ("${GENERAL_COMPRESSOR}" STREQUAL "zstd")
    add_definitions(-DUSE_ZSTD_COMPRESSION=1)
    message(STATUS "Using Zstandard compression")
else()
    message(SEND_ERROR "GENERAL_COMPRESSOR=${GENERAL_COMPRESSOR} is unimplemented.")
endif()

# Add local CMake module directory to CMake's modules path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# Macro providing the length of the absolute source directory path so we can
# create a relative (rather than absolute) __FILE__ macro
string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
add_definitions("-DSOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}")

# Profiling options
add_definitions(-DPROF_ENABLED=0)

# Compile-in debug logging statements
#add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# Flush to disk switch
add_definitions(-DFLUSH_TO_DISK_ENABLED=1)

# Make off_t 64-bit
add_definitions(-D_FILE_OFFSET_BITS=64)

# Ensure we're compiling for a little-endian machine (we don't support big-endian)
include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if (IS_BIG_ENDIAN)
    message(FATAL_ERROR "Big-endian machines are not supported")
endif()

# Detect linking mode (static or shared); Default to static.
set(CLP_USE_STATIC_LIBS ON CACHE BOOL "Whether to link against static libraries")
if (CLP_USE_STATIC_LIBS)
    if (APPLE)
        set(CLP_STATIC_LIBS_UNSUPPORTED_PLATFORM "macOS")
    elseif (EXISTS "/etc/centos-release")
        set(CLP_STATIC_LIBS_UNSUPPORTED_PLATFORM "CentOS")
    endif()

    if (DEFINED CLP_STATIC_LIBS_UNSUPPORTED_PLATFORM)
        message(
            AUTHOR_WARNING
            "Building with static libraries is unsupported on"
            " ${CLP_STATIC_LIBS_UNSUPPORTED_PLATFORM}. Switching to shared libraries.")
        set(CLP_USE_STATIC_LIBS OFF)
    endif()
endif ()
if(CLP_USE_STATIC_LIBS)
    set(CLP_LIBS_STRING "static")
else()
    set(CLP_LIBS_STRING "shared")
endif()
message(STATUS "Building using ${CLP_LIBS_STRING} libraries")

# Link against c++fs if required by the compiler being used
set(STD_FS_LIBS "")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.1.0")
        set(STD_FS_LIBS "stdc++fs")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0.0")
        set(STD_FS_LIBS "c++fs")
    endif ()
endif ()

# Find and setup ANTLR Library
# We build and link to the static library
find_package(ANTLR REQUIRED)
if (ANTLR_FOUND)
    message(STATUS "Found ANTLR ${ANTLR_VERSION}")
else()
    message(FATAL_ERROR "Could not find libraries for ANTLR ${ANTLR4_TAG}")
endif()

# Find and setup Boost Library
if(CLP_USE_STATIC_LIBS)
    set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost 1.81 REQUIRED iostreams program_options filesystem system regex url)
if(Boost_FOUND)
    message(STATUS "Found Boost ${Boost_VERSION}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for Boost")
endif()

# Find and setup fmt
# NOTE:
# - We only try to link to the static library
# - spdlog uses fmt, so their versions need to be kept in-sync
find_package(fmt 8.0.1 REQUIRED)
if(fmt_FOUND)
    message(STATUS "Found fmt ${fmt_VERSION}")
else()
    message(FATAL_ERROR "Could not find static libraries for fmt")
endif()

# Find and setup spdlog
if(CLP_USE_STATIC_LIBS)
    # NOTE: On some Linux distributions (e.g. Ubuntu), the spdlog package only contains a dynamic
    # library. If the `find_package(spdlog)` call below fails, re-run
    # `tools/scripts/lib_install/<dist_name>/install-packages-from-source.sh` to build spdlog from
    # source.
    set(spdlog_USE_STATIC_LIBS ON)
endif()
find_package(spdlog 1.9.2 REQUIRED)
if(spdlog_FOUND)
    message(STATUS "Found spdlog ${spdlog_VERSION}")
else()
    if (CLP_USE_STATIC_LIBS)
        message(FATAL_ERROR "Could not find static libraries for spdlog. You may want to re-run
            `components/core/tools/scripts/lib_install/<dist>/install-packages-from-source.sh`")
    else()
        message(FATAL_ERROR "Could not find libraries for spdlog.")
    endif()
endif()

# Find and setup libarchive
if(CLP_USE_STATIC_LIBS)
    set(LibArchive_USE_STATIC_LIBS ON)
endif()
find_package(LibArchive REQUIRED)
if(LibArchive_FOUND)
    message(STATUS "Found LibArchive ${LibArchive_VERSION}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for LibArchive")
endif()

# Find and setup libcurl
# By default, CURL does not provide static libraries
find_package(CURL 7.68.0 REQUIRED)
if(CURL_FOUND)
    message(STATUS "Found CURL ${CURL_VERSION_STRING}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for CURL")
endif()

# Find OpenSSL
find_package(OpenSSL REQUIRED)
if (OPENSSL_FOUND)
    message(STATUS "Found OpenSSL (${OPENSSL_VERSION})")
else ()
    message(FATAL_ERROR "OpenSSL not found")
endif ()

# Add log surgeon
add_subdirectory(submodules/log-surgeon EXCLUDE_FROM_ALL)

# Find and setup MariaDBClient library
if(CLP_USE_STATIC_LIBS)
    # NOTE: We can't statically link to MariaDBClient since it's GPL
    message(AUTHOR_WARNING "MariaDBClient cannot be statically linked due to its license.")
endif()
find_package(MariaDBClient 3.1.0 REQUIRED)
if(MariaDBClient_FOUND)
    message(STATUS "Found MariaDBClient ${MariaDBClient_VERSION}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for MariaDBClient")
endif()

# Find and setup mongocxx
if(CLP_USE_STATIC_LIBS)
    set(MONGOCXX_TARGET mongo::mongocxx_static)
else()
    set(MONGOCXX_TARGET mongo::mongocxx_shared)
endif()

find_package(mongocxx REQUIRED)
if(mongocxx_FOUND)
    message(STATUS "Found mongocxx ${mongocxx_VERSION}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for mongocxx")
endif()

# Find and setup msgpack
find_package(msgpack-cxx 7.0.0 REQUIRED)
if(msgpack-cxx_FOUND)
    message(STATUS "Found msgpack-cxx ${msgpack-cxx_VERSION}")
else()
    message(FATAL_ERROR "Could not find msgpack-cxx")
endif()

# Add abseil-cpp
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(submodules/abseil-cpp EXCLUDE_FROM_ALL)

# Add simdjson
add_subdirectory(submodules/simdjson EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)

# Add yaml-cpp
add_subdirectory(submodules/yaml-cpp EXCLUDE_FROM_ALL)

# Find and setup ZStd Library
if(CLP_USE_STATIC_LIBS)
    set(ZStd_USE_STATIC_LIBS ON)
endif()
find_package(ZStd 1.4.4 REQUIRED)
if(ZStd_FOUND)
    message(STATUS "Found ZStd ${ZStd_VERSION}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for ZStd")
endif()

# Find and setup LZMA Library
# TODO: Add a script in ./cmake/Modules to properly import LZMA in find_package()'s module mode
if(CLP_USE_STATIC_LIBS)
    set(LIBLZMA_USE_STATIC_LIBS ON)
endif()
find_package(LibLZMA REQUIRED)
if(LIBLZMA_FOUND)
    message(STATUS "Found Lzma ${LIBLZMA_VERSION_STRING}")
    message(STATUS "Lzma library location: ${LIBLZMA_LIBRARIES}")
    message(STATUS "Lzma Include Dir: ${LIBLZMA_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Could not find ${CLP_LIBS_STRING} libraries for Lzma")
endif()
include_directories(${LIBLZMA_INCLUDE_DIRS})

# sqlite dependencies
set(sqlite_DYNAMIC_LIBS "dl;m;pthread")
include(cmake/Modules/FindLibraryDependencies.cmake)
FindDynamicLibraryDependencies(sqlite "${sqlite_DYNAMIC_LIBS}")

add_subdirectory(src/clp/regex_utils)
add_subdirectory(src/clp/string_utils)

add_subdirectory(src/clp/clg)
add_subdirectory(src/clp/clo)
add_subdirectory(src/clp/clp)
add_subdirectory(src/glt/glt)
add_subdirectory(src/clp/make_dictionaries_readable)
add_subdirectory(src/clp_s)
add_subdirectory(src/reducer)

set(SOURCE_FILES_clp_s_unitTest
    src/clp_s/ArchiveReader.cpp
    src/clp_s/ArchiveReader.hpp
    src/clp_s/ArchiveReaderAdaptor.cpp
    src/clp_s/ArchiveReaderAdaptor.hpp
    src/clp_s/ArchiveWriter.cpp
    src/clp_s/ArchiveWriter.hpp
    src/clp_s/ColumnReader.cpp
    src/clp_s/ColumnReader.hpp
    src/clp_s/ColumnWriter.cpp
    src/clp_s/ColumnWriter.hpp
    src/clp_s/DictionaryEntry.cpp
    src/clp_s/DictionaryEntry.hpp
    src/clp_s/DictionaryWriter.cpp
    src/clp_s/DictionaryWriter.hpp
    src/clp_s/FileReader.cpp
    src/clp_s/FileReader.hpp
    src/clp_s/FileWriter.cpp
    src/clp_s/FileWriter.hpp
    src/clp_s/InputConfig.cpp
    src/clp_s/InputConfig.hpp
    src/clp_s/JsonConstructor.cpp
    src/clp_s/JsonConstructor.hpp
    src/clp_s/JsonFileIterator.cpp
    src/clp_s/JsonFileIterator.hpp
    src/clp_s/JsonParser.cpp
    src/clp_s/JsonParser.hpp
    src/clp_s/PackedStreamReader.cpp
    src/clp_s/PackedStreamReader.hpp
    src/clp_s/ReaderUtils.cpp
    src/clp_s/ReaderUtils.hpp
    src/clp_s/Schema.cpp
    src/clp_s/Schema.hpp
    src/clp_s/SchemaMap.cpp
    src/clp_s/SchemaMap.hpp
    src/clp_s/SchemaReader.cpp
    src/clp_s/SchemaReader.hpp
    src/clp_s/SchemaTree.cpp
    src/clp_s/SchemaTree.hpp
    src/clp_s/SchemaWriter.cpp
    src/clp_s/SchemaWriter.hpp
    src/clp_s/search/AddTimestampConditions.cpp
    src/clp_s/search/AddTimestampConditions.hpp
    src/clp_s/search/AndExpr.cpp
    src/clp_s/search/AndExpr.hpp
    src/clp_s/search/BooleanLiteral.cpp
    src/clp_s/search/BooleanLiteral.hpp
    src/clp_s/search/clp_search/EncodedVariableInterpreter.cpp
    src/clp_s/search/clp_search/EncodedVariableInterpreter.hpp
    src/clp_s/search/clp_search/Grep.cpp
    src/clp_s/search/clp_search/Grep.hpp
    src/clp_s/search/clp_search/Query.cpp
    src/clp_s/search/clp_search/Query.hpp
    src/clp_s/search/ColumnDescriptor.cpp
    src/clp_s/search/ColumnDescriptor.hpp
    src/clp_s/search/ConstantProp.cpp
    src/clp_s/search/ConstantProp.hpp
    src/clp_s/search/ConvertToExists.cpp
    src/clp_s/search/ConvertToExists.hpp
    src/clp_s/search/DateLiteral.cpp
    src/clp_s/search/DateLiteral.hpp
    src/clp_s/search/EmptyExpr.cpp
    src/clp_s/search/EmptyExpr.hpp
    src/clp_s/search/EvaluateTimestampIndex.cpp
    src/clp_s/search/EvaluateTimestampIndex.hpp
    src/clp_s/search/Expression.cpp
    src/clp_s/search/Expression.hpp
    src/clp_s/search/FilterExpr.cpp
    src/clp_s/search/FilterExpr.hpp
    src/clp_s/search/FilterOperation.hpp
    src/clp_s/search/Integral.cpp
    src/clp_s/search/Integral.hpp
    src/clp_s/search/Literal.hpp
    src/clp_s/search/NarrowTypes.cpp
    src/clp_s/search/NarrowTypes.hpp
    src/clp_s/search/NullLiteral.cpp
    src/clp_s/search/NullLiteral.hpp
    src/clp_s/search/OrExpr.cpp
    src/clp_s/search/OrExpr.hpp
    src/clp_s/search/OrOfAndForm.cpp
    src/clp_s/search/OrOfAndForm.hpp
    src/clp_s/search/Output.cpp
    src/clp_s/search/Output.hpp
    src/clp_s/search/OutputHandler.cpp
    src/clp_s/search/OutputHandler.hpp
    src/clp_s/search/Projection.cpp
    src/clp_s/search/Projection.hpp
    src/clp_s/search/SchemaMatch.cpp
    src/clp_s/search/SchemaMatch.hpp
    src/clp_s/search/SearchUtils.cpp
    src/clp_s/search/SearchUtils.hpp
    src/clp_s/search/StringLiteral.cpp
    src/clp_s/search/StringLiteral.hpp
    src/clp_s/search/Transformation.hpp
    src/clp_s/search/Value.hpp
    src/clp_s/TimestampDictionaryReader.cpp
    src/clp_s/TimestampDictionaryReader.hpp
    src/clp_s/TimestampDictionaryWriter.cpp
    src/clp_s/TimestampDictionaryWriter.hpp
    src/clp_s/TimestampEntry.cpp
    src/clp_s/TimestampEntry.hpp
    src/clp_s/TimestampPattern.cpp
    src/clp_s/TimestampPattern.hpp
    src/clp_s/Utils.cpp
    src/clp_s/Utils.hpp
    src/clp_s/VariableDecoder.cpp
    src/clp_s/VariableDecoder.hpp
    src/clp_s/VariableEncoder.cpp
    src/clp_s/VariableEncoder.hpp
    src/clp_s/ZstdCompressor.cpp
    src/clp_s/ZstdCompressor.hpp
    src/clp_s/ZstdDecompressor.cpp
    src/clp_s/ZstdDecompressor.hpp
    )

set(SOURCE_FILES_reducer_unitTest
    src/reducer/BufferedSocketWriter.cpp
    src/reducer/BufferedSocketWriter.hpp
    src/reducer/ConstRecordIterator.hpp
    src/reducer/CountOperator.cpp
    src/reducer/CountOperator.hpp
    src/reducer/DeserializedRecordGroup.cpp
    src/reducer/DeserializedRecordGroup.hpp
    src/reducer/GroupTags.hpp
    src/reducer/network_utils.cpp
    src/reducer/network_utils.hpp
    src/reducer/Operator.cpp
    src/reducer/Operator.hpp
    src/reducer/Pipeline.cpp
    src/reducer/Pipeline.hpp
    src/reducer/Record.hpp
    src/reducer/RecordGroup.hpp
    src/reducer/RecordGroupIterator.hpp
    src/reducer/RecordTypedKeyIterator.hpp
    src/reducer/types.hpp
    )

set(SOURCE_FILES_unitTest
        src/clp/Array.hpp
        src/clp/aws/AwsAuthenticationSigner.cpp
        src/clp/aws/AwsAuthenticationSigner.hpp
        src/clp/aws/constants.hpp
        src/clp/BoundedReader.cpp
        src/clp/BoundedReader.hpp
        src/clp/BufferedFileReader.cpp
        src/clp/BufferedFileReader.hpp
        src/clp/BufferReader.cpp
        src/clp/BufferReader.hpp
        src/clp/clp/CommandLineArguments.cpp
        src/clp/clp/CommandLineArguments.hpp
        src/clp/clp/compression.cpp
        src/clp/clp/compression.hpp
        src/clp/clp/decompression.cpp
        src/clp/clp/decompression.hpp
        src/clp/clp/FileCompressor.cpp
        src/clp/clp/FileCompressor.hpp
        src/clp/clp/FileDecompressor.cpp
        src/clp/clp/FileDecompressor.hpp
        src/clp/clp/FileToCompress.hpp
        src/clp/clp/run.cpp
        src/clp/clp/run.hpp
        src/clp/clp/utils.cpp
        src/clp/clp/utils.hpp
        src/clp/CurlDownloadHandler.cpp
        src/clp/CurlDownloadHandler.hpp
        src/clp/CurlEasyHandle.hpp
        src/clp/CurlGlobalInstance.cpp
        src/clp/CurlGlobalInstance.hpp
        src/clp/CurlOperationFailed.hpp
        src/clp/CurlStringList.hpp
        src/clp/database_utils.cpp
        src/clp/database_utils.hpp
        src/clp/Defs.h
        src/clp/dictionary_utils.cpp
        src/clp/dictionary_utils.hpp
        src/clp/DictionaryEntry.hpp
        src/clp/DictionaryReader.hpp
        src/clp/DictionaryWriter.hpp
        src/clp/error_handling/ErrorCode.hpp
        src/clp/EncodedVariableInterpreter.cpp
        src/clp/EncodedVariableInterpreter.hpp
        src/clp/ErrorCode.hpp
        src/clp/ffi/encoding_methods.cpp
        src/clp/ffi/encoding_methods.hpp
        src/clp/ffi/encoding_methods.inc
        src/clp/ffi/ir_stream/byteswap.hpp
        src/clp/ffi/ir_stream/Deserializer.hpp
        src/clp/ffi/ir_stream/decoding_methods.cpp
        src/clp/ffi/ir_stream/decoding_methods.hpp
        src/clp/ffi/ir_stream/decoding_methods.inc
        src/clp/ffi/ir_stream/encoding_methods.cpp
        src/clp/ffi/ir_stream/encoding_methods.hpp
        src/clp/ffi/ir_stream/IrErrorCode.cpp
        src/clp/ffi/ir_stream/IrErrorCode.hpp
        src/clp/ffi/ir_stream/IrUnitHandlerInterface.hpp
        src/clp/ffi/ir_stream/IrUnitType.hpp
        src/clp/ffi/ir_stream/ir_unit_deserialization_methods.cpp
        src/clp/ffi/ir_stream/ir_unit_deserialization_methods.hpp
        src/clp/ffi/ir_stream/protocol_constants.hpp
        src/clp/ffi/ir_stream/Serializer.cpp
        src/clp/ffi/ir_stream/Serializer.hpp
        src/clp/ffi/ir_stream/utils.cpp
        src/clp/ffi/ir_stream/utils.hpp
        src/clp/ffi/KeyValuePairLogEvent.cpp
        src/clp/ffi/KeyValuePairLogEvent.hpp
        src/clp/ffi/SchemaTree.cpp
        src/clp/ffi/SchemaTree.hpp
        src/clp/ffi/search/CompositeWildcardToken.cpp
        src/clp/ffi/search/CompositeWildcardToken.hpp
        src/clp/ffi/search/ExactVariableToken.cpp
        src/clp/ffi/search/ExactVariableToken.hpp
        src/clp/ffi/search/query_methods.cpp
        src/clp/ffi/search/query_methods.hpp
        src/clp/ffi/search/QueryMethodFailed.hpp
        src/clp/ffi/search/QueryToken.hpp
        src/clp/ffi/search/QueryWildcard.cpp
        src/clp/ffi/search/QueryWildcard.hpp
        src/clp/ffi/search/Subquery.cpp
        src/clp/ffi/search/Subquery.hpp
        src/clp/ffi/search/WildcardToken.cpp
        src/clp/ffi/search/WildcardToken.hpp
        src/clp/ffi/utils.cpp
        src/clp/ffi/utils.hpp
        src/clp/ffi/Value.hpp
        src/clp/FileDescriptor.cpp
        src/clp/FileDescriptor.hpp
        src/clp/FileDescriptorReader.cpp
        src/clp/FileDescriptorReader.hpp
        src/clp/FileReader.cpp
        src/clp/FileReader.hpp
        src/clp/FileWriter.cpp
        src/clp/FileWriter.hpp
        src/clp/GlobalMetadataDB.hpp
        src/clp/GlobalMetadataDBConfig.cpp
        src/clp/GlobalMetadataDBConfig.hpp
        src/clp/GlobalMySQLMetadataDB.cpp
        src/clp/GlobalMySQLMetadataDB.hpp
        src/clp/GlobalSQLiteMetadataDB.cpp
        src/clp/GlobalSQLiteMetadataDB.hpp
        src/clp/Grep.cpp
        src/clp/Grep.hpp
        src/clp/hash_utils.cpp
        src/clp/hash_utils.hpp
        src/clp/ir/constants.hpp
        src/clp/ir/EncodedTextAst.cpp
        src/clp/ir/EncodedTextAst.hpp
        src/clp/ir/LogEvent.hpp
        src/clp/ir/LogEventDeserializer.cpp
        src/clp/ir/LogEventDeserializer.hpp
        src/clp/ir/LogEventSerializer.cpp
        src/clp/ir/LogEventSerializer.hpp
        src/clp/ir/parsing.cpp
        src/clp/ir/parsing.hpp
        src/clp/ir/parsing.inc
        src/clp/ir/types.hpp
        src/clp/ir/utils.cpp
        src/clp/ir/utils.hpp
        src/clp/LibarchiveFileReader.cpp
        src/clp/LibarchiveFileReader.hpp
        src/clp/LibarchiveReader.cpp
        src/clp/LibarchiveReader.hpp
        src/clp/LogSurgeonReader.cpp
        src/clp/LogSurgeonReader.hpp
        src/clp/LogTypeDictionaryEntry.cpp
        src/clp/LogTypeDictionaryEntry.hpp
        src/clp/LogTypeDictionaryReader.hpp
        src/clp/LogTypeDictionaryWriter.cpp
        src/clp/LogTypeDictionaryWriter.hpp
        src/clp/math_utils.hpp
        src/clp/MessageParser.cpp
        src/clp/MessageParser.hpp
        src/clp/MySQLDB.cpp
        src/clp/MySQLDB.hpp
        src/clp/MySQLParamBindings.cpp
        src/clp/MySQLParamBindings.hpp
        src/clp/MySQLPreparedStatement.cpp
        src/clp/MySQLPreparedStatement.hpp
        src/clp/networking/socket_utils.cpp
        src/clp/networking/socket_utils.hpp
        src/clp/NetworkReader.cpp
        src/clp/NetworkReader.hpp
        src/clp/PageAllocatedVector.hpp
        src/clp/ParsedMessage.cpp
        src/clp/ParsedMessage.hpp
        src/clp/Platform.hpp
        src/clp/Profiler.cpp
        src/clp/Profiler.hpp
        src/clp/Query.cpp
        src/clp/Query.hpp
        src/clp/ReaderInterface.cpp
        src/clp/ReaderInterface.hpp
        src/clp/ReadOnlyMemoryMappedFile.cpp
        src/clp/ReadOnlyMemoryMappedFile.hpp
        src/clp/spdlog_with_specializations.hpp
        src/clp/SQLiteDB.cpp
        src/clp/SQLiteDB.hpp
        src/clp/SQLitePreparedStatement.cpp
        src/clp/SQLitePreparedStatement.hpp
        src/clp/Stopwatch.cpp
        src/clp/Stopwatch.hpp
        src/clp/streaming_archive/ArchiveMetadata.cpp
        src/clp/streaming_archive/ArchiveMetadata.hpp
        src/clp/streaming_archive/Constants.hpp
        src/clp/streaming_archive/MetadataDB.cpp
        src/clp/streaming_archive/MetadataDB.hpp
        src/clp/streaming_archive/reader/Archive.cpp
        src/clp/streaming_archive/reader/Archive.hpp
        src/clp/streaming_archive/reader/File.cpp
        src/clp/streaming_archive/reader/File.hpp
        src/clp/streaming_archive/reader/Message.cpp
        src/clp/streaming_archive/reader/Message.hpp
        src/clp/streaming_archive/reader/Segment.cpp
        src/clp/streaming_archive/reader/Segment.hpp
        src/clp/streaming_archive/reader/SegmentManager.cpp
        src/clp/streaming_archive/reader/SegmentManager.hpp
        src/clp/streaming_archive/single_file_archive/Defs.hpp
        src/clp/streaming_archive/single_file_archive/writer.cpp
        src/clp/streaming_archive/single_file_archive/writer.hpp
        src/clp/streaming_archive/writer/Archive.cpp
        src/clp/streaming_archive/writer/Archive.hpp
        src/clp/streaming_archive/writer/File.cpp
        src/clp/streaming_archive/writer/File.hpp
        src/clp/streaming_archive/writer/Segment.cpp
        src/clp/streaming_archive/writer/Segment.hpp
        src/clp/streaming_archive/writer/utils.cpp
        src/clp/streaming_archive/writer/utils.hpp
        src/clp/streaming_compression/Compressor.hpp
        src/clp/streaming_compression/Constants.hpp
        src/clp/streaming_compression/Decompressor.hpp
        src/clp/streaming_compression/lzma/Compressor.cpp
        src/clp/streaming_compression/lzma/Compressor.hpp
        src/clp/streaming_compression/lzma/Decompressor.cpp
        src/clp/streaming_compression/lzma/Decompressor.hpp
        src/clp/streaming_compression/lzma/Constants.hpp
        src/clp/streaming_compression/passthrough/Compressor.cpp
        src/clp/streaming_compression/passthrough/Compressor.hpp
        src/clp/streaming_compression/passthrough/Decompressor.cpp
        src/clp/streaming_compression/passthrough/Decompressor.hpp
        src/clp/streaming_compression/zstd/Compressor.cpp
        src/clp/streaming_compression/zstd/Compressor.hpp
        src/clp/streaming_compression/zstd/Constants.hpp
        src/clp/streaming_compression/zstd/Decompressor.cpp
        src/clp/streaming_compression/zstd/Decompressor.hpp
        src/clp/StringReader.cpp
        src/clp/StringReader.hpp
        src/clp/Thread.cpp
        src/clp/Thread.hpp
        src/clp/time_types.hpp
        src/clp/TimestampPattern.cpp
        src/clp/TimestampPattern.hpp
        src/clp/TraceableException.hpp
        src/clp/TransactionManager.hpp
        src/clp/type_utils.hpp
        src/clp/utf8_utils.cpp
        src/clp/utf8_utils.hpp
        src/clp/Utils.cpp
        src/clp/Utils.hpp
        src/clp/VariableDictionaryEntry.cpp
        src/clp/VariableDictionaryEntry.hpp
        src/clp/VariableDictionaryReader.hpp
        src/clp/VariableDictionaryWriter.cpp
        src/clp/VariableDictionaryWriter.hpp
        src/clp/version.hpp
        src/clp/WriterInterface.cpp
        src/clp/WriterInterface.hpp
        submodules/sqlite3/sqlite3.c
        submodules/sqlite3/sqlite3.h
        submodules/sqlite3/sqlite3ext.h
        tests/LogSuppressor.hpp
        tests/TestOutputCleaner.hpp
        tests/test-Array.cpp
        tests/test-BoundedReader.cpp
        tests/test-BufferedFileReader.cpp
        tests/test-clp_s-end_to_end.cpp
        tests/test-clp_s-search.cpp
        tests/test-EncodedVariableInterpreter.cpp
        tests/test-encoding_methods.cpp
        tests/test-error_handling.cpp
        tests/test-ffi_IrUnitHandlerInterface.cpp
        tests/test-ffi_KeyValuePairLogEvent.cpp
        tests/test-ffi_SchemaTree.cpp
        tests/test-FileDescriptorReader.cpp
        tests/test-Grep.cpp
        tests/test-hash_utils.cpp
        tests/test-ir_encoding_methods.cpp
        tests/test-ir_parsing.cpp
        tests/test-ir_serializer.cpp
        tests/test-kql.cpp
        tests/test-main.cpp
        tests/test-math_utils.cpp
        tests/test-MemoryMappedFile.cpp
        tests/test-NetworkReader.cpp
        tests/test-ParserWithUserSchema.cpp
        tests/test-query_methods.cpp
        tests/test-regex_utils.cpp
        tests/test-Segment.cpp
        tests/test-SQLiteDB.cpp
        tests/test-Stopwatch.cpp
        tests/test-StreamingCompression.cpp
        tests/test-string_utils.cpp
        tests/test-sql.cpp
        tests/test-TimestampPattern.cpp
        tests/test-utf8_utils.cpp
        tests/test-Utils.cpp
        )
add_executable(unitTest
        ${SOURCE_FILES_unitTest}
        ${SOURCE_FILES_clp_s_unitTest}
        ${SOURCE_FILES_reducer_unitTest}
        )
target_include_directories(unitTest
        PRIVATE
        ${CMAKE_SOURCE_DIR}/submodules
        )
target_link_libraries(unitTest
        PRIVATE
        absl::flat_hash_map
        Boost::filesystem Boost::iostreams Boost::program_options Boost::regex Boost::url
        ${CURL_LIBRARIES}
        fmt::fmt
        kql
        log_surgeon::log_surgeon
        LibArchive::LibArchive
        MariaDBClient::MariaDBClient
        ${MONGOCXX_TARGET}
        simdjson
        spdlog::spdlog
        sql
        OpenSSL::Crypto
        ${sqlite_LIBRARY_DEPENDENCIES}
        ${STD_FS_LIBS}
        clp::regex_utils
        clp::string_utils
        yaml-cpp::yaml-cpp
        ${LIBLZMA_LIBRARIES}
        ZStd::ZStd
        )
target_compile_features(unitTest
        PRIVATE cxx_std_20
        )
