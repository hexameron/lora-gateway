gateway: gateway.o urlencode.o kml.o global.h
	cc -o gateway gateway.o urlencode.o kml.o -lm -lwiringPi -lcurl -lncurses

gateway.o: gateway.c global.h
	gcc -c gateway.c -Wall

urlencode.o: urlencode.c
	gcc -c urlencode.c -Wall

kml.o: kml.c kml.h global.h 
	gcc -c kml.c -Wall

