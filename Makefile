All: laser-emu slave emu-test

laser-emu: laser-emu.c
slave: slave.c
emu-test: emu-test.c
	$(CC) $^ -g -o $@ 

clean:
	-rm -f laser-emu slave emu-test
