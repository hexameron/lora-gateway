gateway: gateway.o hiperfifo.o habitat.o utils.o utils.h global.h
	cc -o gateway gateway.o hiperfifo.o habitat.o utils.o -lm -lwiringPi -lcurl -lncurses

gateway.o: gateway.c utils.h global.h
	gcc -c gateway.c -Wall

hiperfifo.o: hiperfifo.c hiperfifo.h
	gcc -c hiperfifo.c -Wall

utils.o: utils.c utils.h
	gcc -c utils.c -Wall

habitat.o: habitat.c utils.h hiperfifo.h
	gcc -c habitat.c -Wall 
