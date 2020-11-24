project (FfxVariableShading_DX12)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common.cmake)

set(sources 
    VariableShadingSample.cpp
    VariableShadingSample.h
    VariableShadingCode.cpp
    VariableShadingCode.h
    SampleRenderer.cpp
    SampleRenderer.h
    stdafx.cpp
    stdafx.h)

set(ffx_variableshading_src 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-variableshading/ffx_variable_shading.h
)

set(Shaders_src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-variableshading/ffx_variable_shading.h	
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/GLTFPbrPass-IO.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/VRSImageGenCS.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/VRSOverlay.hlsl
    )

set(Bin_src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/VariableShadingSample.json
    )

function(copyCommand list dest)
    foreach(fullFileName ${list})    
        get_filename_component(file ${fullFileName} NAME)
        message("Generating custom command for ${fullFileName}")
        add_custom_command(
            OUTPUT   ${dest}/${file}
            PRE_BUILD
            COMMAND cmake -E make_directory ${dest}
            COMMAND cmake -E copy ${fullFileName}  ${dest}
            MAIN_DEPENDENCY  ${fullFileName}
            COMMENT "Updating ${file} into ${dest}" 
        )
    endforeach()    
endfunction()

copyCommand("${Shaders_src}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibDX)
copyCommand("${Bin_src}" ${CMAKE_HOME_DIRECTORY}/bin)

source_group("Sources"            FILES ${sources})
source_group("Sources\\GLTF"      FILES ${GLTF_src})
source_group("Shaders"            FILES ${Shaders_src})    
source_group("Bin"                FILES ${Bin_src})    
  
source_group("FFX-VariableShading"				  FILES ${ffx_variableshading_src})
   
add_executable(${PROJECT_NAME} WIN32 ${sources} ${GLTF_src} ${Shaders_src} ${Bin_src} ${ffx_variableshading_src}) 
target_link_libraries (${PROJECT_NAME} LINK_PUBLIC Cauldron_DX12 ImGUI amd_ags DXC d3dcompiler D3D12)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin")

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-variableshading)

addManifest(${PROJECT_NAME})