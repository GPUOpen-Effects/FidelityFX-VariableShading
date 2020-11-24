@ECHO OFF
mkdir DX12_VS2017
cd DX12_VS2017
cmake ..\.. -G "Visual Studio 15 2017" -DGFX_API=DX12 %*
cd ..

mkdir DX12_VS2019
cd DX12_VS2019
cmake ..\.. -DGFX_API=DX12 %*
cd ..
