
ifdef NIOS
cc=nios2-linux-uclibc-gcc -elf2flt
output=daq
$(output): setup8866.o  test.o
	 $(cc)  setup8866.o  test.o -o daq   
test.o: main.c 
	 $(cc) -DNIOS -c main.c -o test.o

else
$(CC) =  gcc -g3 -gdwarf2
all: executable 
debug: CC += -DDEBUG -g
debug: executable 
executable: main.c
	 $(CC) main.c -o  dtdetector
endif

   
clean:
	rm -f test.o $(output)
