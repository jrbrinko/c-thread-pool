CFLAGS = -g
EXECUTABLE = pool_test 


pool_test: pool_test.o pool.o cqueue.o spinlock.o
	gcc -o ${EXECUTABLE} ${CFLAGS} -pthread pool_test.o pool.o cqueue.o spinlock.o

pool_test.o: pool_test.c pool.h
	gcc -c ${CFLAGS} pool_test.c

pool.o: pool.c pool.h cqueue.h
	gcc -c ${CFLAGS} -pthread pool.c

cqueue.o: cqueue.c cqueue.h spinlock.h
	gcc -c ${CFLAGS} cqueue.c

spinlock.o: spinlock.c spinlock.h
	gcc -c ${CFLAGS} spinlock.c 

clean:
	rm -f *.o qmain
	rm -f core*