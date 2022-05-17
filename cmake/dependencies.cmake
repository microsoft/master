set(CMAKE_FOLDER ThirdParty)

include(FetchContent)

# Google Abseil C++ Library
FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp
    GIT_TAG 215105818dfde3174fe799600bb0f3cae233d0bf
)

# Google Protobuf
FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v3.19.3
    SOURCE_SUBDIR cmake
)
set(protobuf_BUILD_TESTS OFF)

# TensorFlow python package
# To update the package version to a nightly build, query https://pypi.org/pypi/tf-nightly-cpu/json and search for the
# SHA and URL of the desired date for the cp37 version. To update the package version to a stable build instead, query
# https://pypi.org/pypi/tensorflow-cpu/json.
if(WIN32)
    FetchContent_Declare(
        tensorflow_whl
        URL https://files.pythonhosted.org/packages/8e/28/3897ab86b0e7676a878d30e7b44f0b4e39992837270a3c630296f6c86b10/tensorflow_cpu-2.9.0-cp37-cp37m-win_amd64.whl
        URL_HASH SHA256=b7998cc046d449324734c464377c74828019fde3a43fc7af0ef6af7fc4ff0298
    )
else()
    FetchContent_Declare(
        tensorflow_whl
        URL https://files.pythonhosted.org/packages/00/b1/61a3642f90e46b87071d80b2abf69229a3ffeb6636e7cee461caf294c476/tensorflow_cpu-2.9.0-cp37-cp37m-manylinux_2_17_x86_64.manylinux2014_x86_64.whl
        URL_HASH SHA256=e5744bcc14b0fc211b8a4477563b4501b0d305e018590ac9adf4900a110741bf
    )
endif()

# DirectX-Headers repo
FetchContent_Declare(
    directx_headers
    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
    GIT_TAG d49ae12ab350b20468a9667bad700f3227cd3f7a
)

# DirectML Redistributable NuGet package
FetchContent_Declare(
    directml_redist
    URL https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.8.2
    URL_HASH SHA256=a58141244b075cc3abfdc247310224b68b64ddd2aaac25ea04ed703deb5d4f9b
)

# DirectMLX helper library
FetchContent_Declare(
    directmlx
    URL https://raw.githubusercontent.com/microsoft/DirectML/d4657006a60a7b7d9baf17638c42aee27258c836/Libraries/DirectMLX.h
    URL_HASH SHA256=01f0c83ab8491e792326166e0b6d59473deecad2a3f8281f79f26f55e1fc613d
    DOWNLOAD_NO_EXTRACT TRUE
)

# WinPixEventRuntime NuGet package
FetchContent_Declare(
    pix_event_runtime
    URL https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.210209001
    URL_HASH SHA256=ee0af78308ea90c31b0c2a0c8814d2bef994e4cbfb5ae6c5b98b50c7fd98e1bc
)

# Download and extract dependencies.
FetchContent_MakeAvailable(
    abseil
    protobuf
    tensorflow_whl
    directx_headers
    directml_redist 
    directmlx
    pix_event_runtime
)

# The DirectX-Headers target assumes dependent targets include headers with the directx prefix 
# (e.g. <directx/d3d12.h>). However, directml.h unconditionally includes "d3d12.h"; this works on
# Windows with the SDK installed, but WSL builds need to resolve this include correctly.
target_include_directories(DirectX-Headers INTERFACE ${directx_headers_SOURCE_DIR}/include/directx)

# Target to add DirectML redist headers to the include path.
add_library(directml_headers INTERFACE)
target_include_directories(directml_headers INTERFACE ${directml_redist_SOURCE_DIR}/include)
add_library(directml_redist::headers ALIAS directml_headers)

# Target to add DirectMLX headers to the include path.
add_library(directmlx_headers INTERFACE)
target_include_directories(directmlx_headers INTERFACE ${directmlx_SOURCE_DIR})
add_library(directmlx::headers ALIAS directmlx_headers)

# Target (for convenience) that adds both DirectML and DirectMLX headers to the include path.
add_library(directml_all_headers INTERFACE)
target_link_libraries(directml_all_headers INTERFACE directml_headers directmlx_headers)
add_library(directml::headers ALIAS directml_all_headers)

# Target to add WinPixEventRuntime headers to the include path.
add_library(pix_headers INTERFACE)
target_include_directories(pix_headers INTERFACE ${pix_event_runtime_SOURCE_DIR}/include)
add_library(pix_event_runtime::headers ALIAS pix_headers)

# Location to generate .pb.h/.pb.cc files from the packaged TF .proto files. The TF wheel
# comes with pre-generated .pb.h files, but we need the source files (not included) when linking.
# This script uses the protobuf dependency above to regenerate C++ code for the .proto files 
# needed by the TFDML plugin. The .pb.h files and protobuf headers in the TF wheel are intentionally
# deleted to avoid accidental usage, since their protobuf version will not likely match the copy
# of protobuf above.
set(tensorflow_generated_protobuf_dir ${tensorflow_whl_BINARY_DIR}/proto)
set(tensorflow_include_dir ${tensorflow_whl_SOURCE_DIR}/tensorflow/include)
file(GLOB_RECURSE tensorflow_whl_pb_h_files ${tensorflow_include_dir}/**/*.pb.h)
if(tensorflow_whl_pb_h_files)
    file(REMOVE ${tensorflow_whl_pb_h_files})
endif()
file(REMOVE_RECURSE ${tensorflow_include_dir}/google/protobuf)

# Target to add TensorFlow headers, generated .pb.h files, and runtime lib. The runtime lib
# contains symbols for the plugin APIs and a few utilities (e.g. logging).
add_library(tensorflow_whl_lib STATIC)
target_include_directories(
    tensorflow_whl_lib 
    PUBLIC
    ${tensorflow_generated_protobuf_dir}
    $<TARGET_PROPERTY:libprotobuf,INCLUDE_DIRECTORIES>
    INTERFACE 
    ${tensorflow_whl_SOURCE_DIR}/tensorflow/include
)
target_link_libraries(
    tensorflow_whl_lib 
    INTERFACE 
    $<$<BOOL:${WIN32}>:${tensorflow_whl_SOURCE_DIR}/tensorflow/python/_pywrap_tensorflow_internal.lib>
    $<$<BOOL:${UNIX}>:${tensorflow_whl_SOURCE_DIR}/tensorflow/python/_pywrap_tensorflow_internal.so>
    $<$<BOOL:${UNIX}>:${tensorflow_whl_SOURCE_DIR}/tensorflow/libtensorflow_framework.so.2>
    libprotobuf
)
add_library(tensorflow_whl::lib ALIAS tensorflow_whl_lib)

# Introduces a command to generate C++ code for a .proto file in the TF wheel.
function(tf_proto_cpp proto_path)
    cmake_path(GET proto_path STEM proto_stem)
    cmake_path(GET proto_path PARENT_PATH proto_parent_dir)
    cmake_path(SET proto_generated_h ${tensorflow_generated_protobuf_dir}/${proto_parent_dir}/${proto_stem}.pb.h)
    cmake_path(SET proto_generated_cc ${tensorflow_generated_protobuf_dir}/${proto_parent_dir}/${proto_stem}.pb.cc)

    add_custom_command(
        OUTPUT 
            ${proto_generated_h} 
            ${proto_generated_cc}
        COMMAND 
            protobuf::protoc 
            --proto_path=${tensorflow_include_dir}
            --cpp_out=${tensorflow_generated_protobuf_dir} 
            ${proto_path}
        DEPENDS
            ${tensorflow_include_dir}/${proto_path}
        COMMENT
            "Generating C++ code for ${proto_path}"
    )

    target_sources(tensorflow_whl_lib PRIVATE ${proto_generated_h} ${proto_generated_cc})
endfunction()

# Generate the necessary .proto files in the TF wheel (performed at build time).
tf_proto_cpp(tensorflow/core/profiler/protobuf/xplane.proto)
tf_proto_cpp(tensorflow/core/framework/graph.proto)
tf_proto_cpp(tensorflow/core/framework/function.proto)
tf_proto_cpp(tensorflow/core/framework/attr_value.proto)
tf_proto_cpp(tensorflow/core/framework/tensor.proto)
tf_proto_cpp(tensorflow/core/framework/resource_handle.proto)
tf_proto_cpp(tensorflow/core/framework/tensor_shape.proto)
tf_proto_cpp(tensorflow/core/framework/types.proto)
tf_proto_cpp(tensorflow/core/framework/node_def.proto)
tf_proto_cpp(tensorflow/core/framework/full_type.proto)
tf_proto_cpp(tensorflow/core/framework/op_def.proto)
tf_proto_cpp(tensorflow/core/framework/versions.proto)
tf_proto_cpp(tensorflow/core/framework/kernel_def.proto)
tf_proto_cpp(tensorflow/core/grappler/costs/op_performance_data.proto)
tf_proto_cpp(tensorflow/core/protobuf/device_properties.proto)

# A python interpreter is required to produce the plugin wheel. This python environment
# must have the 'wheel' package installed.
find_package(Python 3.6 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND "${Python_EXECUTABLE}" "-c" "import wheel"
    RESULT_VARIABLE python_wheel_check_exit_code
    OUTPUT_QUIET
)
if(NOT ${python_wheel_check_exit_code} EQUAL 0)
    message(
        FATAL_ERROR 
        "The python interpreter at '${Python_EXECUTABLE}' does not "
        "have the 'wheel' package installed."
    )
endif()

set(CMAKE_FOLDER "")