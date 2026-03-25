if [ ! -d "build" ]; then
	mkdir build
fi

cd build
cmake -DBUILD_ALL=ON -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++  ..
make
