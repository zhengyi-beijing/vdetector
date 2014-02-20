
ifdef NIOS
	 cc=nios2-linux-uclibc-gcc -elf2flt
	 output=daq
     $(output): setup.o  test.o
	 $(cc)  setup.o  test.o -o daq    #nios2-linux-uclibc-gcc  setup.o  daq_socket.o  -o daq  -elf2flt
     test.o: main.c 
	 $(cc) -c main.c -o test.o

else
	 cc=gcc
	 output=dtdetector
     $(output): main.c
	 $(cc)  main.c -o $(output)
endif

   
clean:
	rm -f test.o $(output)
