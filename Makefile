All: laser-emu slave emu-test

laser-emu: laser-emu.c
	$(CC) $^ -g -o $@ 

slave: slave.c
	$(CC) $^ -g -o $@ 

emu-test: emu-test.c
	$(CC) $^ -g -o $@ 

clean:
	-rm -f laser-emu slave emu-test
