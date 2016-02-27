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
//compile like: gcc -pthread -o pthread_example pthread_example.c
//
//Some (but not all) compilers will work with: gcc -o pthread_example pthread_example.c -lpthread
//
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//IMPORTANT!!!!!!!!!!!!!!!!!!!!!!!
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
//


//(modern C++ has std::atomic if you just want atomic access to a single variable)
//(it also has std::thread... you can use that instead of pthreads if you like)
int global_finish_counter = 0;
pthread_mutex_t global_finish_counter_mutex;



typedef struct JoinMeArg
{
	int an_int;
	int sleep_duration;
	char a_string[16];
} JoinMeArg;

void* joinMe(void* my_arg_pointer)
{
	JoinMeArg my_arg;
	memcpy(&my_arg, my_arg_pointer, sizeof(JoinMeArg));
	free(my_arg_pointer);
	
	printf("\n\nStarted a joinMe() thread. Its an_int = %d, its a_string = %s\n", 
		my_arg.an_int, my_arg.a_string);

	sleep(my_arg.sleep_duration);
	
	printf("\n\nA joinMe() thread slept for %d seconds, and is about to return. Its an_int = %d, its a_string = %s\n", 
		my_arg.sleep_duration, my_arg.an_int, my_arg.a_string);
	
	//blocks until acquired (or failure)
	if(!pthread_mutex_lock(&global_finish_counter_mutex)) //successfully acquired
	{
		global_finish_counter++;
		printf("This joinMe() thread is thread #%d to finish.\n\n", global_finish_counter);
		pthread_mutex_unlock(&global_finish_counter_mutex);
	}
}



typedef struct OnMyOwnArg
{
	char a_char;
} OnMyOwnArg;

void* onMyOwn(void* my_arg)
{
	char my_char = ((OnMyOwnArg*)my_arg)->a_char;
	free(my_arg);
	
	printf("\n\nStarted an onMyOwn() thread. Its my_char = %c\n", my_char);
	
	sleep(3);
	printf("\n\nAn onMyOwn() thread slept for 1 second, and is about to return. Its my_char = %c\n", my_char);
	
	//blocks until acquired (or failure)
	if(!pthread_mutex_lock(&global_finish_counter_mutex)) //successfully acquired
	{
		global_finish_counter++;
		printf("This onMyOwn() thread is thread #%d to finish.\n\n", global_finish_counter);
		pthread_mutex_unlock(&global_finish_counter_mutex);
	}
}



int main()
{
	pthread_mutex_init(&global_finish_counter_mutex, 0);
	
	
	pthread_t join_me1, join_me2, join_me3, on_my_own1, on_my_own2;
	
	
	
	printf("\n\nEXAMPLE 1:\n=============\n");
	JoinMeArg* join_me_arg1 = (JoinMeArg*)malloc(sizeof(JoinMeArg));
	join_me_arg1->an_int = 1;
	strcpy(join_me_arg1->a_string, "please join me!");
	
	printf("\n\nWill now launch a single joinMe() thread, and join it.\n");
	printf("How long should the sleep() inside this joinMe() last? (seconds): ");
	scanf("%d", &join_me_arg1->sleep_duration);

	pthread_create(&join_me1, 0, joinMe, join_me_arg1);
	pthread_join(join_me1, 0); //(returns once thread join_me1 has terminated)

	
	
	

	printf("Done with example 1. Enter any integer to continue.\n");
	int junk;
	scanf("%d", &junk);
	
	
	
	
	
	printf("\n\nEXAMPLE 2:\n=============\n");
	OnMyOwnArg* on_my_own_arg1 = (OnMyOwnArg*)malloc(sizeof(OnMyOwnArg));
	on_my_own_arg1->a_char = 'x';
	
	JoinMeArg* join_me_arg2 = (JoinMeArg*)malloc(sizeof(JoinMeArg));
	join_me_arg2->an_int = 2;
	strcpy(join_me_arg2->a_string, "join me too!");
	
	printf("\n\nWill now launch a joinMe(), then an onMyOwn(), then join the joinMe().\n");
	printf("How long should the sleep() inside this joinMe() last? (seconds): ");
	scanf("%d", &join_me_arg2->sleep_duration);

	pthread_create(&join_me2, 0, joinMe, join_me_arg2);
	pthread_create(&on_my_own1, 0, onMyOwn, on_my_own_arg1);
	//if you neither detach nor join a thread, some resources don't get freed (until the process exits)
	pthread_detach(on_my_own1); //(returns immediately)
	pthread_join(join_me2, 0); //(returns once thread join_me2 has terminated)

	
	
	
	printf("Done with example 2. Enter any integer to continue.\n");
	scanf("%d", &junk);
	
	
	
	
	printf("\n\nEXAMPLE 3:\n=============\n");
	OnMyOwnArg* on_my_own_arg2 = (OnMyOwnArg*)malloc(sizeof(OnMyOwnArg));
	on_my_own_arg2->a_char = 'z';
	
	JoinMeArg* join_me_arg3 = (JoinMeArg*)malloc(sizeof(JoinMeArg));
	join_me_arg3->an_int = 3;
	strcpy(join_me_arg3->a_string, "join me three!");

	printf("Will now launch a joinMe(), then join it, then launch an onMyOwn().\n");
	printf("How long should the sleep() inside this joinMe() last? (seconds): ");
	scanf("%d", &join_me_arg3->sleep_duration);
	
	pthread_create(&join_me3, 0, joinMe, join_me_arg3);
	pthread_join(join_me3, 0); //(returns once thread join_me2 has terminated)
	pthread_create(&on_my_own2, 0, onMyOwn, on_my_own_arg2);
	//if you neither detach nor join a thread, some resources don't get freed (until the process exits)
	pthread_detach(on_my_own2); //(returns immediately)
	

	
	
	
	
	
	pthread_mutex_destroy(&global_finish_counter_mutex);
	return 0;
}