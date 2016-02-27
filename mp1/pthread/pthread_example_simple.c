#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
//
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//IMPORTANT!!!!!!!!!!!!!!!!!!!!!!!
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
//compile like: gcc -pthread -o pthread_example_simple pthread_example_simple.c
//
//Some (but not all) compilers will work with: gcc -o pthread_example_simple pthread_example_simple.c -lpthread
//
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//IMPORTANT!!!!!!!!!!!!!!!!!!!!!!!
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
//


void* threadFunction(void* my_arg)
{
	while(1)
	{
		sleep(1);
		printf("In the threadFunction() thread. Passed value = %lu\n", (unsigned long int)my_arg);
		//directly using the void* argument is quick, hacky, and limited; 
		//see the other example for the right way
	}
}

int main()
{
	pthread_t the_thread;
	unsigned long int thread_arg;
	
	printf("What integer to pass to the created thread? ");
	scanf("%lu", &thread_arg);

	pthread_create(&the_thread, 0, threadFunction, (void*)thread_arg);

	while(1)
	{
		sleep(2);
		printf("Hello from the main thread!\n");
	}
	
	return 0;
}