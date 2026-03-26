rm build -r

if [ ! -d "build" ]; then
	mkdir build
fi

cd build

cmake	-DBUILD_LEPTON_DATA_COLLECTOR=ON \
	-DBUILD_LEPTON_CONTROLLER=ON \
	-DBUILD_SERVER=ON \
	-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
	-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++  ..

make

cmake	-DBUILD_SDK=ON \
	-DBUILD_LEPTON_SDK=ON \
	-DBUILD_DEMO=ON ..

make
