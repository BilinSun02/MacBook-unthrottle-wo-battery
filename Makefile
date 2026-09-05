CXX := xcrun --sdk macosx clang++
SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
ARCH := arm64e
BUILD := build
KERNEL_HEADERS := $(SDKROOT)/System/Library/Frameworks/Kernel.framework/Headers

.PHONY: all cli kext clean

all: cli

cli:
	@mkdir -p $(BUILD)
	$(CXX) -std=c++17 -Wall -Wextra -Iinclude \
		cli/main.cpp -framework IOKit -framework CoreFoundation \
		-o $(BUILD)/mbu

# Experimental plain-Make kext build. Xcode SDK/KPI details can vary by
# macOS/Xcode release; see docs/BUILD.md before treating this target as stable.
kext:
	@test -d "$(KERNEL_HEADERS)" || (echo "Kernel headers not found in SDK" && false)
	@mkdir -p $(BUILD)/MBUnthrottle.kext/Contents/MacOS $(BUILD)/obj
	$(CXX) -x c++ -arch $(ARCH) -std=c++17 -O2 \
		-DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ -mkernel -fapple-kext \
		-fno-builtin -fno-exceptions -fno-rtti -fno-common \
		-fno-use-cxa-atexit -nostdinc \
		-I$(KERNEL_HEADERS) -Iinclude -Ikext \
		-c kext/MBUnthrottleService.cpp -o $(BUILD)/obj/service.o
	$(CXX) -x c++ -arch $(ARCH) -std=c++17 -O2 \
		-DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ -mkernel -fapple-kext \
		-fno-builtin -fno-exceptions -fno-rtti -fno-common \
		-fno-use-cxa-atexit -nostdinc \
		-I$(KERNEL_HEADERS) -Iinclude -Ikext \
		-c kext/MBUnthrottleUserClient.cpp -o $(BUILD)/obj/userclient.o
	$(CXX) -arch $(ARCH) -Xlinker -kext -nostdlib -lkmod -lcc_kext \
		$(BUILD)/obj/service.o $(BUILD)/obj/userclient.o \
		-o $(BUILD)/MBUnthrottle.kext/Contents/MacOS/MBUnthrottle
	cp kext/Info.plist $(BUILD)/MBUnthrottle.kext/Contents/Info.plist

clean:
	rm -rf $(BUILD)
