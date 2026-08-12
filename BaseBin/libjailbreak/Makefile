TARGET = libjailbreak.dylib

CC = clang
CXX = clang++

CFLAGS = -framework Foundation -framework CoreServices -framework Security -framework IOKit -framework IOSurface -framework IOMobileFramebuffer -framework CoreGraphics -framework ImageIO -lMobileGestalt -I../.include -I../_external/modules/litehook/src -isysroot $(shell xcrun --sdk iphoneos --show-sdk-path) -arch arm64 -arch arm64e -miphoneos-version-min=15.0 -fobjc-arc -I$(shell brew --prefix)/opt/libarchive/include -g -O2
CXXFLAGS = $(CFLAGS) -std=c++11
LDFLAGS = -larchive -lbsm -L../.build -F ../_external/frameworks -lchoma -lc++ -dynamiclib -install_name @loader_path/$(TARGET)

CSRCS = $(wildcard src/*.c src/roothider/*.c ../_external/modules/litehook/src/*.c)
MSRCS = $(wildcard src/*.m src/roothider/*.m)
ASRCS = $(wildcard src/*.S)
CPPSRCS = $(wildcard src/roothider/*.cpp)

OBJS = $(CSRCS:.c=.o) $(MSRCS:.m=.o) $(ASRCS:.S=.o) $(CPPSRCS:.cpp=.o)

sign: $(TARGET)
	@ldid -S $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.m
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	@rm -f $(TARGET) $(OBJS)
