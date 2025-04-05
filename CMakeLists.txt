cmake_minimum_required(VERSION 3.25)

project(LATE VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)


if(CMAKE_HOST_APPLE)
    message(STATUS "Build host: Apple")
endif()
if(CMAKE_HOST_LINUX)
    message(STATUS "Build host: Linux")
    message(FATAL_ERROR "Linux build host is not currently supported")
endif()

if(APPLE)
    message(STATUS "Target: Apple")
endif()
if(LINUX)
    message(STATUS "Target: Linux")
endif()


if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES)
        list(FIND CMAKE_OSX_ARCHITECTURES "x86_64" INDEX_X86_64)
        list(FIND CMAKE_OSX_ARCHITECTURES "arm64" INDEX_ARM64)
        if(INDEX_X86_64 GREATER -1)
            set(DARWIN_X86_64 TRUE)
        endif()
        if(INDEX_ARM64 GREATER -1)
            set(DARWIN_ARM64 TRUE)
        endif()
    else()
        message(STATUS "No architectures explicitly selected, default to x86_64 and arm64.")
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS" FORCE)
        set(DARWIN_X86_64 TRUE)
        set(DARWIN_ARM64 TRUE)
    endif()
endif()


if(LINUX AND USE_GLIBC)
    option(USE_CUDA "Use CUDA under Linux (glibc)" OFF)
endif()


# generates assembly to embed data file
function(generate_embedding_assembly assembly name path)
    if(APPLE)
        add_custom_command(
            OUTPUT ${assembly}
            COMMAND echo ".section __DATA,__${name}" > ${assembly}
            COMMAND echo ".globl _${name}_start" >> ${assembly}
            COMMAND echo "_${name}_start:" >> ${assembly}
            COMMAND echo ".incbin \\\"${path}\\\"" >> ${assembly}
            COMMAND echo ".globl _${name}_end" >> ${assembly}
            COMMAND echo "_${name}_end:" >> ${assembly}
            DEPENDS ${path}
            COMMENT "Generate assembly for embedding ${name}"
        )
    elseif(LINUX)
        add_custom_command(
            OUTPUT ${assembly}
            COMMAND echo ".section .data.${name}" > ${assembly}
            COMMAND echo ".global ${name}_start" >> ${assembly}
            COMMAND echo "${name}_start:" >> ${assembly}
            COMMAND echo ".incbin \\\"${path}\\\"" >> ${assembly}
            COMMAND echo ".global ${name}_end" >> ${assembly}
            COMMAND echo "${name}_end:" >> ${assembly}
            DEPENDS ${path}
            COMMENT "Generate assembly for embedding ${name}"
        )
    endif()
endfunction()

# tar gzip path and embed with a name
function(embed_tar target name path)
    enable_language(ASM)
    set(output_dir "${CMAKE_BINARY_DIR}/embedded_data")
    file(MAKE_DIRECTORY "${output_dir}")
    set(archive "${output_dir}/embed_${name}.tgz")

    add_custom_command(
        OUTPUT "${archive}"
        # COMMAND ${CMAKE_COMMAND} -E tar cvzf "${archive}" "${path}"
        COMMAND tar czf "${archive}" --dereference -C "${path}" .
        # COMMAND ${CMAKE_COMMAND} -E tar cvzf "${archive}" "$(realpath ${path})" # TODO: resolve only input path symlink to be able to have symlinks inside the tar
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS "${path}"
        COMMENT "Creating tarball for embedded data ${name}"
    )

    add_custom_target(embed_${name} ALL DEPENDS "${archive}")

    set(assembly "${output_dir}/embed_${name}.s")

    generate_embedding_assembly("${assembly}" "${name}" "${archive}")

    add_dependencies(${target} "embed_${name}")

    target_sources(${target} PRIVATE "${assembly}")
endfunction()





# ONNX Runtime: https://github.com/microsoft/onnxruntime
if(APPLE)
    set(ONNX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/onnxruntime/dist-darwin CACHE STRING "Path to ONNX runtime binary distribution")
elseif(LINUX)
    if(USE_MUSL_LIBC)
        set(ONNX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/onnxruntime/dist-linux-x86_64-musl CACHE STRING "Path to ONNX runtime binary distribution")
    elseif(USE_GLIBC)
        set(ONNX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/onnxruntime/dist-linux-x86_64-glibc-dbg CACHE STRING "Path to ONNX runtime binary distribution")
    endif()
endif()

set(ONNX_INCLUDE_DIR ${ONNX_DIR}/include)
set(ONNX_LIBRARY_DIR ${ONNX_DIR}/lib)

file(GLOB ONNX_LIBRARIES "${ONNX_LIBRARY_DIR}/lib*.a")
# list(REMOVE_ITEM ONNX_LIBRARIES ${ONNX_LIBRARY_DIR}/libcpuinfo.a)



# whisper.cpp: https://github.com/ggerganov/whisper.cpp
if(APPLE)
    set(WHISPER_X86_64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/whisper/dist-darwin-x86_64 CACHE STRING "Path to whisper.cpp x86_64 distribution")
    set(WHISPER_ARM64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/whisper/dist-darwin-arm64 CACHE STRING "Path to whisper.cpp arm64 distribution")

    set(WHISPER_X86_64_INCLUDE_DIR ${WHISPER_X86_64_DIR}/include CACHE STRING "Path to whisper.cpp x86_64 distribution include files")
    set(WHISPER_X86_64_LIBRARY_DIR ${WHISPER_X86_64_DIR}/lib CACHE STRING "Path to whisper.cpp x86_64 distribution library files")

    set(WHISPER_ARM64_INCLUDE_DIR ${WHISPER_ARM64_DIR}/include CACHE STRING "Path to whisper.cpp arm64 distribution include files")
    set(WHISPER_ARM64_LIBRARY_DIR ${WHISPER_ARM64_DIR}/lib CACHE STRING "Path to whisper.cpp arm64 distribution library files")
elseif(LINUX)
    if(USE_MUSL_LIBC)
        set(WHISPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/whisper/dist-linux-x86_64-musl CACHE STRING "Path to whisper.cpp binary distribution")
    elseif(USE_GLIBC)
        if(USE_CUDA)
            set(WHISPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/whisper/dist-linux-x86_64-glibc-cuda CACHE STRING "Path to whisper.cpp binary distribution")
        else()
            set(WHISPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/whisper/dist-linux-x86_64-glibc CACHE STRING "Path to whisper.cpp binary distribution")
        endif()
    endif()
endif()

set(WHISPER_LIBRARY_DIR ${WHISPER_DIR}/lib)
# set(WHISPER_LIBRARY_DIR ${WHISPER_DIR}/lib/static)
if(APPLE)
    if(DARWIN_X86_64)
        set(WHISPER_INCLUDE_DIR ${WHISPER_X86_64_DIR}/include)
        file(GLOB WHISPER_X86_64_LIBRARIES "${WHISPER_X86_64_LIBRARY_DIR}/lib*.a")
    endif()
    if(DARWIN_ARM64)
        # this will override x86_64, but that is fine - they both must be equal
        set(WHISPER_INCLUDE_DIR ${WHISPER_ARM64_DIR}/include)
        file(GLOB WHISPER_ARM64_LIBRARIES "${WHISPER_ARM64_LIBRARY_DIR}/lib*.a")
    endif()
elseif(LINUX)
    set(WHISPER_INCLUDE_DIR ${WHISPER_DIR}/include)
    file(GLOB WHISPER_LIBRARIES "${WHISPER_LIBRARY_DIR}/lib*.a")
    list(REMOVE_ITEM WHISPER_LIBRARIES ${WHISPER_LIBRARY_DIR}/libggml-cuda.a)
endif()



# OpenBLAS: https://github.com/OpenMathLib/OpenBLAS
if(LINUX)
    if(USE_MUSL_LIBC)
        set(OPENBLAS_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/deps/openblas/build-linux-x86_64-musl/lib/libopenblas.a" CACHE STRING "Path to OpenBLAS library")
    elseif(USE_GLIBC)
        set(OPENBLAS_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/deps/openblas/build-linux-x86_64-glibc/lib/libopenblas.a" CACHE STRING "Path to OpenBLAS library")
    endif()
endif()








add_executable(late)

target_include_directories(late PRIVATE
    deps/sqlite
    deps/json/include
    deps/popl/include
    deps/utfcpp/source
    deps/spdlog/include
    deps/cppack/msgpack/include
    deps/cpp-httplib
    deps/dr_libs

    ${WHISPER_INCLUDE_DIR}
    ${ONNX_INCLUDE_DIR}
)

set(C_SOURCES
    deps/sqlite/sqlite3.c
    deps/UtfConv.c
    src/vfs/untar.c
    src/vfs/vfs.c
    src/vfs/zlib.c
)

set(CPP_SOURCES
    src/sqlite/sqlite.cpp
    src/vad/vad.cpp
    src/storage.cpp
    src/string_util.cpp
    src/utf8_util.cpp
    src/wav_util.cpp
    src/whisper.cpp
    src/main.cpp
)

target_sources(late PRIVATE ${C_SOURCES})
set_source_files_properties(${C_SOURCES} PROPERTIES LANGUAGE C)

target_sources(late PRIVATE ${CPP_SOURCES})
set_source_files_properties(${CPP_SOURCES} PROPERTIES LANGUAGE CXX)


# embed static folder
embed_tar(late "_vfs_static" "${CMAKE_CURRENT_SOURCE_DIR}/static")




# required by data embedding
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> <FLAGS> -o <OBJECT> -c <SOURCE>")


if(APPLE)
    message(STATUS "Target platform: Darwin (${CMAKE_OSX_ARCHITECTURES})")

    target_link_options(late PRIVATE -Xlinker -w) # suppress all warning (too much pointless warnings about missing architectures)

    target_link_libraries(late PRIVATE
        "-framework CoreML"
        "-framework Metal"
        "-framework MetalKit"
        "-framework MetalPerformanceShaders"
        "-framework MetalPerformanceShadersGraph"
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework Accelerate"

        # $<LINK_GROUP:RESCAN,$<LINK_LIBRARY:WHOLE_ARCHIVE,${ONNX_LIBRARIES}>>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,${ONNX_LIBRARIES}>

        $<IF:$<BOOL:${DARWIN_X86_64}>,${WHISPER_X86_64_LIBRARIES},>
        $<IF:$<BOOL:${DARWIN_ARM64}>,${WHISPER_ARM64_LIBRARIES},>

        z
        objc
    )
elseif(LINUX)
    if(USE_MUSL_LIBC)
        message(STATUS "Target platform: Linux (musl libc)")
        # set_target_properties(late PROPERTIES LINK_FLAGS "-static")
        set(SYSTEM_LIBRARIES gomp)
    elseif(USE_GLIBC)
        if(USE_CUDA)
            message(STATUS "Target platform: Linux (glibc + CUDA)")

            # CUDA drivers, i.e., CUDA Toolkit libraries
            add_library(cuda-drivers SHARED src/dummy.cpp)
            target_link_libraries(cuda-drivers PRIVATE
                -static-libgcc -static-libstdc++ -Wl,-Bstatic -lz -Wl,-Bdynamic
                -L${CMAKE_SYSROOT}/usr/local/cuda/lib64
                -lcudart_static
                -Wl,--whole-archive
                -lcublas_static -lcublasLt_static
            )
            target_link_options(cuda-drivers PRIVATE -rdynamic)

            # PyTorch and whisper.cpp CUDA interface
            add_library(late-cuda SHARED src/dummy.cpp)
            target_link_libraries(late-cuda PRIVATE
                -Wl,--whole-archive
                # TODO: get rid of WHOLE_ARCHIVE ? how does it compare to -Wl,--whole-archive ?
                $<IF:$<BOOL:${USE_CUDA}>,$<LINK_LIBRARY:WHOLE_ARCHIVE,${WHISPER_LIBRARY_DIR}/libggml-cuda.a>,>
                -Wl,--no-whole-archive
                -static-libgcc -static-libstdc++ -Wl,-Bstatic -Wl,-Bdynamic
                -L${CMAKE_SYSROOT}/usr/local/cuda/lib64
                -lcudart_static # required separately for each dynamic library using CUDA
                cuda-drivers    # link against the full set of CUDA drivers dynamic library above
            )
            set_target_properties(late-cuda PROPERTIES
                INSTALL_RPATH "$ORIGIN"
                BUILD_WITH_INSTALL_RPATH ON
            )
            target_link_options(late-cuda PRIVATE -rdynamic)

            # set_target_properties(late-cuda PROPERTIES LINK_FLAGS "-mcmodel=large -Wl,--gc-sections -Wl,--no-undefined -Wl,--warn-unresolved-symbols -Wl,--warn-once")


            set_target_properties(late PROPERTIES
                INSTALL_RPATH "$ORIGIN"
                BUILD_WITH_INSTALL_RPATH ON
            )
            target_link_options(late PRIVATE -rdynamic)
            target_compile_definitions(late PRIVATE USE_CUDA) # expose USE_CUDA to preprocessor
        else()
            message(STATUS "Target platform: Linux (glibc)")
        endif()

        # set(SYSTEM_LIBRARIES -static-libgcc -static-libstdc++ -Wl,-Bstatic gomp stdc++fs -lz -Wl,-Bdynamic rt pthread dl)
        set(SYSTEM_LIBRARIES -static-libgcc -static-libstdc++ -Wl,-Bstatic gomp -Wl,-Bdynamic rt pthread dl)

        # set_target_properties(late PROPERTIES LINK_FLAGS "-static-libgcc -static-libstdc++")

        set_target_properties(late PROPERTIES LINK_FLAGS "-mcmodel=large -Wl,--gc-sections -Wl,--no-undefined -Wl,--warn-unresolved-symbols -Wl,--warn-once")
        # NOTE: --gc-sections is essential
    endif()

    # target_compile_options(late PRIVATE -static)
    # target_link_options(late PRIVATE -Wl,--as-needed)

    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0 -mcmodel=large -fPIC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")

    target_link_libraries(late PRIVATE
        $<LINK_GROUP:RESCAN,$<LINK_LIBRARY:WHOLE_ARCHIVE,${ONNX_LIBRARIES}>>

        ${WHISPER_LIBRARIES}
        ${OPENBLAS_LIBRARY}
        ${SYSTEM_LIBRARIES}

    )

    # NOTE: no linking aganst late-cuda dynamic library, so that it can be optionally loaded during runtime
    # target_link_libraries(late PRIVATE -Wl,--as-needed late-cuda)

endif()


add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/models"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/models" "${CMAKE_BINARY_DIR}/models"
    DEPENDS "${CMAKE_SOURCE_DIR}/models"
    COMMENT "Creating symlink for models"
    VERBATIM
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/static"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/static" "${CMAKE_BINARY_DIR}/static"
    DEPENDS "${CMAKE_SOURCE_DIR}/static"
    COMMENT "Creating symlink for static"
    VERBATIM
)

add_custom_target(dev_run_symlinks ALL DEPENDS "${CMAKE_BINARY_DIR}/models" "${CMAKE_BINARY_DIR}/static")

