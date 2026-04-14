call "D:\software\vs studio\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

where cl
where nmake

cmake -G "NMake Makefiles" ^
    -DBUILD_SDK=ON ^
    -DBUILD_LEPTON_SDK=ON ^
    -DBUILD_DEMO=ON ^
    -DCMAKE_INSTALL_PREFIX=./sdk_output ^
    -DCMAKE_BUILD_TYPE=Debug ^
    ..

nmake

nmake install
