cmake_minimum_required(VERSION 3.4)
set(CMAKE_GENERATOR_PLATFORM x64)

project (FfxVariableShading_${GFX_API})

option(BUILD_INSTALLER "Generate the installer build files")

# ouput exe to bin directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/bin )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# reference libs used by both backends
add_subdirectory(libs/cauldron)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

if(GFX_API STREQUAL DX12)
    add_subdirectory(src/DX12)
elseif(GFX_API STREQUAL VK)
    find_package(Vulkan REQUIRED)
    add_subdirectory(src/VK)
else()
    message(STATUS "----------------------------------------------------------------------------------------")
    message(STATUS "")
    message(STATUS "** Almost there!!")
    message(STATUS "")
    message(STATUS " This sample does not support VULKAN yet. Once it does, you need to invoke cmake in one of these ways:")
    message(STATUS "")
    message(STATUS " Examples:")
    message(STATUS "    cmake <project_root_dir> -DGFX_API=DX12")
    message(STATUS "    cmake <project_root_dir> -DGFX_API=VK")
    message(STATUS "")
    message(STATUS "----------------------------------------------------------------------------------------")
    message(FATAL_ERROR "")
endif()

if(BUILD_INSTALLER)
	install(DIRECTORY . DESTINATION .
			PATTERN ".git/*" EXCLUDE
			PATTERN "*.ilk" EXCLUDE
			PATTERN "*.pdb" EXCLUDE
			PATTERN "*.docx" EXCLUDE
			PATTERN "*.rtf" EXCLUDE
			PATTERN "*.pptx" EXCLUDE
			PATTERN "README.md" EXCLUDE
			PATTERN "doc/*" EXCLUDE
			PATTERN "doc" EXCLUDE
			PATTERN "build/DX12*/*" EXCLUDE
			PATTERN "build/DX12*" EXCLUDE
			PATTERN "build/VK*/*" EXCLUDE
			PATTERN "build/VK*" EXCLUDE
			PATTERN "bin/ShaderLibDX/ShaderCacheDX/*" EXCLUDE
			PATTERN "bin/ShaderLibDX/ShaderCacheDX" EXCLUDE
			PATTERN "bin/ShaderLibVK/*" EXCLUDE
			PATTERN "bin/ShaderLibVK" EXCLUDE
			PATTERN "src/VK/*" EXCLUDE
			PATTERN "src/VK" EXCLUDE
			PATTERN "*.git" EXCLUDE
			PATTERN "*.gitattributes" EXCLUDE
			PATTERN "*.gitignore" EXCLUDE
			PATTERN "*.gitlab-ci.yml" EXCLUDE
			PATTERN "*.gitmodules" EXCLUDE)
	set(CPACK_WIX_UPGRADE_GUID "95FD61D5-E7AE-4BAE-98F1-C39CC21FD091")
	set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../license.txt")
	set(CPACK_PACKAGE_FILE_NAME "AMD FidelityFX Variable Shading")
	set(CPACK_PACKAGE_NAME "AMD FidelityFX Variable Shading")
	set(CPACK_PACKAGE_VENDOR "Advanced Micro Devices, Inc.")
	set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AMD FidelityFX Variable Shading")
	set(CPACK_PACKAGE_DESCRIPTION "")
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")
	set(CPACK_PACKAGE_VERSION_MAJOR "0")
	set(CPACK_PACKAGE_VERSION_MINOR "9")
	set(CPACK_PACKAGE_VERSION_PATCH "0")
	set(CPACK_GENERATOR WIX)
	include(CPack)
endif(BUILD_INSTALLER)