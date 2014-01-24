     cc=nios2-linux-uclibc-gcc -elf2flt


     test: setup.o  test.o
	 $(cc)  setup.o  test.o -o test    #nios2-linux-uclibc-gcc  setup.o  daq_socket.o  -o daq  -elf2flt

#test.o: test.c  hardware.h
     test.o: main.c 
	 @echo now to update daq_socket.o
	 $(cc) -c main.c -o test.o
   
#   setup.o:  setup.c   hardware.h
#	 $(cc) -c setup.c


   clean:
	rm -f *.o daq
