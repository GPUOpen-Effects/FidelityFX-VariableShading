# FidelityFX Variable Shading Sample 

A small demo to show integration and usage of the [FidelityFX Variable Shading library](https://github.com/GPUOpen-Effects/FidelityFX-VariableShading/tree/master/ffx-variableshading).

![Screenshot](screenshot.png)

# Build Instructions

### Prerequisites

To build this sample, the following tools are required:

- [CMake 3.4](https://cmake.org/download/)
- [Visual Studio 2017](https://visualstudio.microsoft.com/downloads/)
- [Windows 10 SDK 10.0.18362.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)

Then follow these steps:

1) Clone the repository with its submodules:
    ```
    > git clone https://github.com/GPUOpen-Effects/FidelityFX-VariableShading.git --recurse-submodules
    ```

2) Generate the solutions:
    ```
    > cd FidelityFX-VariableShading\sample\build
    > GenerateSolutions.bat
    ```

3) Open the solution in the DX12_VS2017 or DX12_VS2019 directory, compile and run.

