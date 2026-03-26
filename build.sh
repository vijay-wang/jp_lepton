rm build -r
rm build_sdk -r

if [ ! -d "build" ]; then
	mkdir build
fi

if [ ! -d "build_sdk" ]; then
	mkdir build_sdk
fi

cd build

cmake	-DBUILD_LEPTON_DATA_COLLECTOR=ON \
	-DBUILD_LEPTON_CONTROLLER=ON \
	-DBUILD_SERVER=ON \
	-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
	-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
	-DCMAKE_INSTALL_PREFIX=./output -DCMAKE_BUILD_TYPE="Debug" ..

make
make install

cd -
cd build_sdk
cmake	-DBUILD_SDK=ON \
	-DBUILD_LEPTON_SDK=ON \
	-DBUILD_DEMO=ON \
	-DCMAKE_INSTALL_PREFIX=./sdk_output -DCMAKE_BUILD_TYPE="Debug" ..

make
make install
