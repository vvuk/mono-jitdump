CFLAGS=-O1 -g
LDFLAGS=

ifndef USE_REAL_MONO
# We have copies of the needed structs inside the code directly, so that we can build without
# needing mono source. Flag the symbols for dynamic lookup because we won't link against
# the mono dylib at build time.
LDFLAGS+=\
	-Wl,-U,_mono_method_full_name \
	-Wl,-U,_mono_profiler_create \
	-Wl,-U,_mono_profiler_set_jit_done_callback \
	-Wl,-U,_mono_profiler_set_domain_unloaded_callback \
	$(NULL)

#-Wl,-undefined,dynamic_lookup

else
# Use real mono sources to build
MONO_ROOT=/Users/vladimir/proj/unity-mono
UNITY_ROOT=/opt/UnitySrc/u/unity-22

CFLAGS+=\
	-DUSE_REAL_MONO=1 \
	-I$(UNITY_ROOT)/External/MonoBleedingEdge/builds/include/mono \
	-I$(UNITY_ROOT)/External/il2cpp/builds/libmono/config \
	-I$(UNITY_ROOT)/External/il2cpp/builds/external/mono/mono/eglib \
	-I$(UNITY_ROOT)/External/il2cpp/builds/libil2cpp/os/c-api \
	-I$(MONO_ROOT) \
	$(NULL)

LDFLAGS+=\
	-L$(UNITY_ROOT)/External/MonoBleedingEdge/builds/embedruntimes/osx-arm64 \
	-lmonobdwgc-2.0 \
	$(NULL)
endif

all: build/libmono-profiler-jitdump.dylib

build/libmono-profiler-jitdump.dylib: mono-profiler-jitdump.c
	mkdir -p build
	clang $(CFLAGS) $(LDFLAGS) -dynamiclib -o $@ $^
# in case we're linking with the real mono, fix up the rpath entry for libmonobdwgc
	install_name_tool -change @executable_path/../Frameworks/MonoEmbedRuntime/osx/libmonobdwgc-2.0.dylib \
							  @executable_path/../Frameworks/MonoBleedingEdge/MonoEmbedRuntime/osx/libmonobdwgc-2.0.dylib $@
							  

build/jitdump-dump: jitdump-dump.c
	mkdir -p build
	clang -O1 -o $@ $^

clean: 
	rm -f libmono-profiler-jitdump.dylib jitdump-dump
