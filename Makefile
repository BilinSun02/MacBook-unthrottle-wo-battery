CC := xcrun --sdk macosx clang
CXX := xcrun --sdk macosx clang++
SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
ARCH := arm64e
BUILD := build
KERNEL_HEADERS := $(SDKROOT)/System/Library/Frameworks/Kernel.framework/Headers

.PHONY: all cli kext clean verify-kext

all: cli verify-kext

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
		-DKERNEL -D__KERNEL__ -mkernel -fapple-kext \
		-fno-builtin -fno-exceptions -fno-rtti -fno-common \
		-fno-use-cxa-atexit \
		-I$(KERNEL_HEADERS) -Iinclude -Ikext \
		-c kext/MBUnthrottleService.cpp -o $(BUILD)/obj/service.o
	$(CXX) -x c++ -arch $(ARCH) -std=c++17 -O2 \
		-DKERNEL -D__KERNEL__ -mkernel -fapple-kext \
		-fno-builtin -fno-exceptions -fno-rtti -fno-common \
		-fno-use-cxa-atexit \
		-I$(KERNEL_HEADERS) -Iinclude -Ikext \
		-c kext/MBUnthrottleUserClient.cpp -o $(BUILD)/obj/userclient.o
	$(CC) -x c -arch $(ARCH) -O2 \
		-DKERNEL -D__KERNEL__ -mkernel \
		-fno-builtin -fno-common \
		-I$(KERNEL_HEADERS) -Iinclude -Ikext \
		-c kext/MBUnthrottle_info.c -o $(BUILD)/obj/kmod_info.o
	$(CXX) -arch $(ARCH) -nostdlib -Xlinker -kext \
		$(BUILD)/obj/service.o \
		$(BUILD)/obj/userclient.o \
		-lkmodc++ \
		$(BUILD)/obj/kmod_info.o \
		-lkmod -lcc_kext \
		-o $(BUILD)/MBUnthrottle.kext/Contents/MacOS/MBUnthrottle
	cp kext/Info.plist $(BUILD)/MBUnthrottle.kext/Contents/Info.plist

verify-kext: kext
	@echo "Checking kmod bookkeeping symbols..."
	@nm -g $(BUILD)/MBUnthrottle.kext/Contents/MacOS/MBUnthrottle | grep -q ' _kmod_info$$' || \
		(echo "ERROR: _kmod_info not found" && false)
	@nm $(BUILD)/MBUnthrottle.kext/Contents/MacOS/MBUnthrottle | \
		grep -E '(_kmod_info|_realmain|_antimain|_kext_apple_cc)' || true
	@plutil -lint $(BUILD)/MBUnthrottle.kext/Contents/Info.plist

clean:
	rm -rf $(BUILD)
