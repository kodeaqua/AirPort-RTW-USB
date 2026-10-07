# Stage 2 probe kext (standalone). Reuses MacKernelSDK from the AirPort-RTW clone
# (run AirPort-RTW/scripts/bootstrap-deps.sh first). Does not touch the PCIe build.
#   make usbprobe  ->  build/out/RTL8188EUProbe.kext
ROOT   := $(shell pwd)
MKSDK  := $(ROOT)/AirPort-RTW/MacKernelSDK
SDK    := $(shell xcrun --show-sdk-path)
OUT    := $(ROOT)/build/out/RTL8188EUProbe.kext
OBJ    := $(ROOT)/build/usb
FLAGS  := -arch x86_64 -mmacosx-version-min=13.0 -fno-exceptions -fno-rtti -fno-stack-protector -mkernel \
          -isysroot $(SDK) -I$(MKSDK)/Headers -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers
CXXF   := $(FLAGS) -fapple-kext -std=c++17 -DKERNEL -Wall -Wno-deprecated-declarations -Wno-nullability-completeness

.PHONY: usbprobe clean
usbprobe: $(OUT)/Contents/MacOS/RTL8188EUProbe

$(OUT)/Contents/MacOS/RTL8188EUProbe: $(OBJ)/RTL8188EUProbe.o $(OBJ)/kmod_info.o kext/RTL8188EUProbe.kext/Contents/Info.plist
	@mkdir -p $(dir $@)
	xcrun clang++ -arch x86_64 -static -nostdlib -Xlinker -kext $(MKSDK)/Library/x86_64/libkmod.a \
	    -Xlinker -undefined -Xlinker dynamic_lookup -o $@ $(OBJ)/RTL8188EUProbe.o $(OBJ)/kmod_info.o
	rsync -a --exclude=MacOS kext/RTL8188EUProbe.kext/ $(OUT)/
	@echo "  OK   $(OUT)"

$(OBJ)/RTL8188EUProbe.o: src/usb/RTL8188EUProbe.cpp src/usb/RTL8188EUProbe.hpp
	@mkdir -p $(OBJ)
	xcrun clang++ $(CXXF) -c $< -o $@
$(OBJ)/kmod_info.o: src/usb/kmod_info.c
	@mkdir -p $(OBJ)
	xcrun clang $(FLAGS) -c $< -o $@

clean:
	rm -rf build
