OBJS := vanity_graphics.o oldskool_graphics.o vert.o frag.o volk.o
WIN_OBJS := $(OBJS:.o=.exe.o)
a.out : $(OBJS) main.o
	gcc -o $@ $^ -lSDL2 -lm

plottest : $(OBJS) plottest.o plot.o
	gcc -o $@ $^ -lSDL2 -lm

a.exe : $(WIN_OBJS) main.exe.o
	/usr/bin/x86_64-w64-mingw32-gcc -o $@ $^ -lSDL2 -lm

main.exe.o main.o : oldskool_graphics.h vanity_graphics_private.h volk.h vector_math.h
vanity_graphics.exe.o vanity_graphics.o : vanity_graphics_private.h volk.h vector_math.h
oldskool_graphics.exe.o oldskool_graphics.o : oldskool_graphics.h vanity_graphics_private.h volk.h vector_math.h vert.h frag.h 
volk.exe.o volk.o : volk.h

%.o : %.c
	gcc -g -c -o $@ $< -Wall -Wno-sign-compare -msse4.2 -O0

%.o : %.spv
	ld -r -b binary -o $@ $< -z noexecstack

%.exe.o : %.c
	/usr/bin/x86_64-w64-mingw32-gcc -g -c -o $@ $< -Wall -Wno-sign-compare -msse4.2 -O0 -Ivulkansdk

%.exe.o : %.spv
	/usr/bin/x86_64-w64-mingw32-ld -r -b binary -o $@ $<

%.h : %.spv
	printf "extern char _binary_$*_spv_start[];\nextern char _binary_$*_spv_end[];\n" > $@

%.spv : shader.%
	glslangValidator -V -o $@ $<

.PHONY: clean
clean:
	rm -f a.out a.exe $(OBJS) $(WIN_OBJS) vert.spv frag.spv vert.h frag.h plottest.o plot.o main.o main.exe.o

.NOTINTERMEDIATE : vert.spv frag.spv
