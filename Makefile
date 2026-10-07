# Stage 2 probe kext (standalone). Reuses MacKernelSDK from the AirPort-RTW clone
# (run AirPort-RTW/scripts/bootstrap-deps.sh first). Does not touch the PCIe build.
#   make usbprobe  ->  build/out/RTL8188EUProbe.kext
ROOT   := $(shell pwd)
MKSDK  := $(ROOT)/AirPort-RTW/MacKernelSDK
SDK    := $(shell xcrun --show-sdk-path)
OUT    := $(ROOT)/build/out/RTL8188EUProbe.kext
OBJ    := $(ROOT)/build/usb
FLAGS  := -arch x86_64 -mmacosx-version-min=13.0 -fno-exceptions -fno-rtti -fno-stack-protector -mkernel \
          -I$(ROOT)/build/fw -isysroot $(SDK) -I$(MKSDK)/Headers -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers
CXXF   := $(FLAGS) -fapple-kext -std=c++17 -DKERNEL -Wall -Wno-deprecated-declarations -Wno-nullability-completeness

.PHONY: usbprobe load unload install uninstall clean
usbprobe: $(OUT)/Contents/MacOS/RTL8188EUProbe

$(OUT)/Contents/MacOS/RTL8188EUProbe: $(OBJ)/RTL8188EUProbe.o $(OBJ)/kmod_info.o kext/RTL8188EUProbe.kext/Contents/Info.plist
	@mkdir -p $(dir $@)
	xcrun clang++ -arch x86_64 -static -nostdlib -Xlinker -kext $(MKSDK)/Library/x86_64/libkmod.a \
	    -Xlinker -undefined -Xlinker dynamic_lookup -o $@ $(OBJ)/RTL8188EUProbe.o $(OBJ)/kmod_info.o
	rsync -a --exclude=MacOS kext/RTL8188EUProbe.kext/ $(OUT)/
	@echo "  OK   $(OUT)"

$(ROOT)/build/fw/rtl8188eu_fw.h:
	./scripts/fetch-rtl8188eu-fw.sh

$(OBJ)/RTL8188EUProbe.o: src/usb/RTL8188EUProbe.cpp src/usb/RTL8188EUProbe.hpp $(ROOT)/build/fw/rtl8188eu_fw.h
	@mkdir -p $(OBJ)
	xcrun clang++ $(CXXF) -c $< -o $@
$(OBJ)/kmod_info.o: src/usb/kmod_info.c
	@mkdir -p $(OBJ)
	xcrun clang $(FLAGS) -c $< -o $@

# Load from a root-owned copy so build/ stays writable by the normal user.
STAGE := /private/tmp/RTL8188EUProbe-load
load: usbprobe
	sudo rm -rf $(STAGE)
	mkdir -p $(STAGE)
	cp -R $(OUT) $(STAGE)/
	sudo chown -R root:wheel $(STAGE)
	sudo kextutil -v $(STAGE)/RTL8188EUProbe.kext

# Persistent install (survives reboot; needed when macOS asks for approval + restart).
# After this: approve in System Settings > Privacy & Security if prompted, then restart.
INSTALLED := /Library/Extensions/RTL8188EUProbe.kext
install: usbprobe
	sudo rm -rf $(INSTALLED)
	sudo cp -R $(OUT) $(INSTALLED)
	sudo chown -R root:wheel $(INSTALLED)
	sudo chmod -R 755 $(INSTALLED)
	sudo kmutil load -p $(INSTALLED) || true
	@echo "If macOS asks: approve in Privacy & Security, then restart."

uninstall:
	-sudo kmutil unload -b io.github.kodeaqua.RTL8188EUProbe
	sudo rm -rf $(INSTALLED)
	@echo "Restart to rebuild the kext collection without it."

unload:
	sudo kextunload -b io.github.kodeaqua.RTL8188EUProbe

clean:
	rm -rf build
