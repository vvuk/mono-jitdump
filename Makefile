MONO_ROOT=/Users/vladimir/proj/unity-mono
UNITY_ROOT=/opt/UnitySrc/u/unity-22

CFLAGS=\
	-O1 -g \
	-I$(UNITY_ROOT)/External/MonoBleedingEdge/builds/include/mono \
	-I$(UNITY_ROOT)/External/il2cpp/builds/libmono/config \
	-I$(UNITY_ROOT)/External/il2cpp/builds/external/mono/mono/eglib \
	-I$(UNITY_ROOT)/External/il2cpp/builds/libil2cpp/os/c-api \
	-I$(MONO_ROOT) \
	$(NULL)

LDFLAGS=\
	-L$(UNITY_ROOT)/External/MonoBleedingEdge/builds/embedruntimes/osx-arm64 \
	-lmonobdwgc-2.0 \
	$(NULL)

all: libmono-profiler-jitdump.dylib

libmono-profiler-jitdump.dylib: mono-profiler-jitdump.c
	clang $(CFLAGS) $(LDFLAGS) -dynamiclib -o $@ $^
	install_name_tool -change @executable_path/../Frameworks/MonoEmbedRuntime/osx/libmonobdwgc-2.0.dylib @executable_path/../Frameworks/MonoBleedingEdge/MonoEmbedRuntime/osx/libmonobdwgc-2.0.dylib libmono-profiler-jitdump.dylib

jitdump-dump: jitdump-dump.c
	clang -O1 -o $@ $^

clean: 
	rm -f libmono-profiler-jitdump.dylib jitdump-dump
