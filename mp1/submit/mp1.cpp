#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fstream> // ifstream
#include <string>
#include <sstream> // istringstream
#include <vector>
#include <thread>
#include <chrono>
#include <ctime> // time()
#include <iostream>
#include <pthread.h>
#include <utility>
#include <queue>
#include <list>
#include <unordered_map>

using std::ifstream;
using std::string;
using std::istringstream;
using std::vector;
using std::cout;
using std::endl;
using std::cin;
using std::priority_queue;
using std::pair;
using std::make_pair;
using std::list;
using std::to_string;
using std::unordered_map;
using std::queue;

/* Global Variables */
#define MAXBUFLEN 1000 // max len of message to receive
static int message_sent; // number of sent messages
static string ordering; // ordered multicast type: causal or total
static int total_proc; // number of processes in the chat room
static int min; // min value of delay
static int delay_range; // range of delay
static int master_proc_id; // this process's ID
static int message_deliver_order = 1; // order number in this process

static vector< priority_queue< pair<int, string> > > holdback_pq; // holdback queue for fifo ordering
static list<pair< vector<int>, string> > holdback_causal; // holdback queue for causal ordering
static vector< unordered_map<int, string> > holdback_total; //holdback queue for total ordering, each elem in arr is a ma: key=message_id, value=message
static priority_queue< pair< int, pair<int, int> > > deliver_q; // deliver q to store message with order added to be delivered: proc1 own message and received message
static vector<int> vec_clock; // this process's own vectorstamps
static vector<struct MultiCastProc*> proc_info; // each process's info

pthread_mutex_t mutexA = PTHREAD_MUTEX_INITIALIZER; // init mutex

/* Function Declaration */

// send messages
int unicast_send(struct MultiCastProc& dest, const char* message, int rand_delay);
void* unicast_send_delay(void* data);
void msend(string& message);
void* morder(void* void_m);

// listen and delvier messages
int unicast_receive(struct MultiCastProc& src);
void* listening(void* data);
void* listeningHandle(void* data);
void holdbackDeliverFIFO(int sender);
void messageDeliverFIFO(string& message, int sender);
int compareVecClock(vector<int>& my_clock, vector<int>& received_clock, int sender_id);
void holdbackDeliverCausal(int master_id, int sender_id);
void* delivering(void*);
void deliverTotal();

/* Structs */
struct MultiCastProc
{
    int proc_id;
    string proc_ip;
    string proc_port;
};

struct ListenData
{
    vector<struct MultiCastProc*> proc_arr;
    int master_proc;
};

struct SendData
{
    vector<struct MultiCastProc*> proc_arr;
    int proc_id;
    string message;
};

/*======= sending message ==============*/

/* unicast send a message to a dest proc */
int unicast_send(struct MultiCastProc* dest, const char* message, int rand_delay)
{
    // delay sending
    //    printf("calling unicast_send ...\n");
    //    std::this_thread::sleep_for(std::chrono::seconds(rand_delay*dest->proc_id)); // extra traffic at p1 to demo causal and total ordering
    std::this_thread::sleep_for(std::chrono::seconds(rand_delay));
    
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    /* init host info */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    /*  convert host info from text strings to binary
     int getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
     */
    if ((rv = getaddrinfo(dest->proc_ip.c_str(), dest->proc_port.c_str(), &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        /* init a socket, return -1 if error */
        if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1)
        {
            perror("talker: socket");
            continue;
        }
        
        break;
    }
    // no valid host
    if (p == NULL)
    {
        fprintf(stderr, "talker: failed to create socket\n");
        return -1;
    }
    /* send data over a socket
     ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
     */
    if ((numbytes = sendto(sockfd, message, strlen(message), 0, p->ai_addr, p->ai_addrlen)) == -1)
    {
        perror("talker: sendto");
        exit(1);
    }
    else
    {
        //        printf("%s sent to %d with delay %d\n", message, dest->proc_id, rand_delay);
    }
    
    freeaddrinfo(servinfo);
    
    //    printf("talker: sent %d bytes to %s\n", numbytes, HOST);
    close(sockfd);
    return 1;
}

/* unicast send with delay, built on top of unicast send */
void* unicast_send_delay(void* data)
{
    //    printf("calling unicast_send_delay ...\n");
    struct SendData* sendD = (struct SendData*)data;
    // generate rand delay
    int rand_delay = rand() % delay_range + min;
    // send message
    unicast_send(sendD->proc_arr[sendD->proc_id-1], sendD->message.c_str(), rand_delay);
    return NULL;
}

/* send message to all procs, built on top of unicast send delay 
 Different send options are available for different ordering
 */
void msend(string& message)
{
    //    printf("calling msend ...\n");
    
    // use multithreading here, delay of message 1 should not block the sending of other message
    pthread_t p_send [total_proc-1];
    // data for total ordering
    string order_message = "";
    // data for causal ordering
    vector<int> sent_vector; // for delivering of sent message at master proc
    
    for (int i = 0; i< total_proc; i++)
    {
        if (i != master_proc_id-1)
        {
            /* init struct to store send data */
            struct SendData* sendD = new SendData;
            sendD->message = message;
            sendD->proc_arr = proc_info;
            sendD->proc_id = i+1;
            
            // get cur time
            time_t cur_t = time(0);
            /* choose ordering to send message */
            // fifo ordering
            if (ordering.compare("fifo") == 0)
            {
                // print send info
                printf("deliver message in fifo ordering ...\n");
                printf("%s\n", ("Sent \"" + message + "\" to process " + std::to_string(sendD->proc_id) + ", system time is " + std::to_string(cur_t)).c_str());
                // message = #message_sent + sender_id + message
                sendD->message = std::to_string(message_sent) + " " + std::to_string(master_proc_id) + " " + message;
                pthread_create(&p_send[i], NULL, unicast_send_delay, (void*)sendD);
            }
            // causal ordering
            else if (ordering.compare("causal") == 0)
            {
                string message_causal = "";
                
                /* lock for changable global variables */
                pthread_mutex_lock(&mutexA);
                // add vector clock to message
                for (int i = 0; i<total_proc;i++)
                {
                    if (i == master_proc_id-1)
                    {
                        message_causal += to_string(message_sent) + " ";
                        sent_vector.push_back(message_sent);
                    }
                    else
                    {
                        message_causal += to_string(vec_clock[i]) + " ";
                        sent_vector.push_back(vec_clock[i]);
                    }
                }
                pthread_mutex_unlock(&mutexA);
                
                // add sender_id + message to message
                message_causal += to_string(master_proc_id) + " " + message;
                printf("%s\n", ("Sent \"" + message_causal + "\" to process " + to_string(sendD->proc_id) + ", system time is " + std::to_string(cur_t)).c_str());
                sendD->message = message_causal;
                pthread_create(&p_send[i], NULL, unicast_send_delay, (void*)sendD);
                pthread_detach(p_send[i]);
            }
            // total ordering
            else if (ordering.compare("total") == 0)
            {
//                printf("calling total ordering ...\n");
                
                /* lock for changable global variables */
                pthread_mutex_lock(&mutexA);
                // message add 'message' + sender_id + #message_sent + cur_time + message, then deliver order
                sendD->message = std::to_string(master_proc_id) + " " +  to_string(message_sent) + " " + sendD->message;
                order_message = sendD->message;
                string message1 = "message " + sendD->message;
                printf("%s\n", ("Sent \"" + message1 + "\" to process " + to_string(sendD->proc_id) +", system time is " + to_string(cur_t)).c_str());
                /* if this proc is not sequencer, then push sent message to
                holdback queue, and wait for order from sequencer to deliver message */
                if (master_proc_id != 1)
                {
                    holdback_total[master_proc_id-1][message_sent] = message;
                }
                pthread_mutex_unlock(&mutexA);
                
                sendD->message = message1;
                pthread_create(&p_send[i], NULL, unicast_send_delay, (void*)sendD);
                pthread_detach(p_send[i]);
            }
            
        }
    }
    
    // deliver message at master proc if causal ordering is using
    if (ordering.compare("causal") == 0)
    {
        pthread_mutex_lock(&mutexA);
        holdback_causal.push_back(make_pair(sent_vector, message));
        holdbackDeliverCausal(master_proc_id, master_proc_id);
        pthread_mutex_unlock(&mutexA);
    }
    
    // sequencer send order message if this proc is 1 and total ordering is using
    if (master_proc_id == 1 && ordering.compare("total") == 0)
    {
        printf("sequencer send order message ...\n");
        struct SendData* tmp = new struct SendData;
        tmp->message = order_message;
        tmp->proc_id = master_proc_id;
        pthread_t p_morder;
        pthread_create(&p_morder, NULL, morder, (void*)tmp);
        pthread_detach(p_morder);
    }
}

/* for total ordering, deliver messages at this proc, and 
 send order message to other proc if this proc is sequencer */
void* morder(void* data)
{
    //    printf("calling morder ...\n");
    
    // sleep for 10s before deliver
    std::this_thread::sleep_for(std::chrono::seconds(10));
    struct SendData* tmp = (struct SendData*)data;
    time_t cur_t = time(0);
    printf("%s\n", ("Deliver \"" + tmp->message + "\" from process " + std::to_string(tmp->proc_id) + ", system time is " + to_string(cur_t)).c_str());
    
    pthread_t p_deliver [total_proc-1];
    /* deliver the message to other proc */
    for (int i = 0; i< total_proc; i++)
    {
        if (i != master_proc_id-1)
        {
            struct SendData* sendD = new SendData;
            sendD->message = tmp->message;
            sendD->proc_arr = proc_info;
            sendD->proc_id = i+1;
            // generate random delay
            int rand_delay = rand() % delay_range + min;
            
            pthread_mutex_lock(&mutexA);
            string message2 = "order " + to_string(message_deliver_order) + " " + sendD->message;
            
            printf("%s\n", ("Order \"" + message2 + "\" to process " + std::to_string(sendD->proc_id) + " with delay " + std::to_string(rand_delay) +", system time is " + to_string(cur_t)).c_str());
            pthread_mutex_unlock(&mutexA);
            // send message with delay
            sendD->message = message2;
            pthread_create(&p_deliver[i], NULL, unicast_send_delay, (void*)sendD);
            pthread_detach(p_deliver[i]);
        }
    }
    
    pthread_mutex_lock(&mutexA);
    message_deliver_order++;
    pthread_mutex_unlock(&mutexA);
    return NULL;
}

/*====== receiving message ============*/

/* receive a message from src */
int unicast_receive(struct MultiCastProc* src)
{
    printf("calling unicast_receive ...\n");
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char message_received[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM; // socket type = SOCK_DGRAM
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(src->proc_ip.c_str(), src->proc_port.c_str(), &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("listener: socket");
            continue;
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("listener: bind");
            continue;
        }
        break;
    }
    
    if (p == NULL)
    {
        fprintf(stderr, "listener: failed to bind socket\n");
        return -1;
    }
    
    freeaddrinfo(servinfo);
    
    //    printf("listener: waiting to recvfrom...\n");
    
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(sockfd, message_received, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
        perror("recvfrom");
        exit(1);
    }
    message_received[numbytes] = '\0';
    
    /* here push the new message along with its sender message count into a pq, not deliver message here
     call function holdbackDeliver(sender_id) to deliver message once they are valid */
    
    string message_str(message_received); // original message contains vector clock and sender_id
    
    //    printf("pushing in holdback_pq ...\n");
    

    /* choose which ordering to deliver message */
    time_t cur_t = time(0);
    // fifo ordering
    if (ordering.compare("fifo") == 0)
    {
        istringstream split_m(message_str);
        int sender_message_count_int;
        int sender_id_int;
        int start = 0; // start pos of message
        split_m >> sender_message_count_int >> sender_id_int;
        start += to_string(sender_id_int).length() + 1;
        start += to_string(sender_message_count_int).length() + 1;
        string message = message_str.substr(start);
        
        pthread_mutex_lock(&mutexA);
        holdback_pq[sender_id_int-1].push(make_pair(-sender_message_count_int, message_str));
        holdbackDeliverFIFO(sender_id_int-1);
        pthread_mutex_unlock(&mutexA);
    }
    // causal ordering
    else if (ordering.compare("causal") == 0)
    {
        // for causal ordering, use #vec_clock of cur proc as key for ordering
        istringstream split_m(message_str);
        vector<int> sender_message_count_int(total_proc,0);
        int sender_id_int;
        int start = 0; // start pos of message
        for (int i = 0; i<total_proc;i++)
        {
            split_m >> sender_message_count_int[i];
            start += to_string(sender_message_count_int[i]).length() + 1;
        }
        split_m >> sender_id_int;
        start += to_string(sender_id_int).length() + 1;
        string message = message_str.substr(start);
        
        printf("%s\n", ("Received message \"" + message + "\" from process " + to_string(sender_id_int) + ", message count " + to_string(sender_message_count_int[sender_id_int-1]) + ", system time is " + std::to_string(cur_t)).c_str());
        
        pthread_mutex_lock(&mutexA);
        holdback_causal.push_back(make_pair(sender_message_count_int, message));
        holdbackDeliverCausal(src->proc_id, sender_id_int);
        pthread_mutex_unlock(&mutexA);
    }
    // total ordering
    else if (ordering.compare("total") == 0)
    {
        istringstream split_m(message_str);
        string token1;
        int sender_message_count_int;
        int sender_id_int;
        int start = 0; // start pos of message
        split_m >> token1;
        start += token1.length() + 1;
        if (token1.compare("message") == 0)
        {
            split_m >> sender_id_int >> sender_message_count_int;
            start += to_string(sender_id_int).length() + 1 + to_string(sender_message_count_int).length() + 1;
            string message = message_str.substr(start);
            printf("%s\n", ("Received message \"" + message + "\" from process " + to_string(sender_id_int) + ", message count " + to_string(sender_message_count_int) + ", system time is " + std::to_string(cur_t)).c_str());
            
            // if sequencer receives a message
            if (master_proc_id == 1)
            {
                string message2 = to_string(sender_id_int) + " " + to_string(sender_message_count_int) + " " + message;
                struct SendData* tmp = new struct SendData;
                tmp->message = message2;
                tmp->proc_id = sender_id_int;
                pthread_t p_morder;
                pthread_create(&p_morder, NULL, morder, (void*)tmp);
                pthread_detach(p_morder);
            }
            // if other proc receives message
            else
            {
                // printf("other proc receive message ...\n");
                // add message to holdback map
                pthread_mutex_lock(&mutexA);
                holdback_total[sender_id_int-1][sender_message_count_int] = message;
                pthread_mutex_unlock(&mutexA);
            }
        }
        /* other proc receive order message,
         sequencer will never receive order message */
        else if (token1.compare("order") == 0)
        {
            int start = 0;
            //            printf("calling order ...\n");
            int message_deliver_order_received;
            split_m >> message_deliver_order_received >> sender_id_int >> sender_message_count_int;
            start += token1.length() + 1 + to_string(sender_id_int).length() + 1 + to_string(sender_message_count_int).length() + 1 + to_string(message_deliver_order_received).length() + 1;
            string message = message_str.substr(start);
            printf("%s\n", ("Received order \"" + message + "\" from process " + to_string(sender_id_int) + ", message count is " + to_string(sender_message_count_int) + " order is " + to_string(message_deliver_order_received) + ", system time is " + to_string(cur_t)).c_str());
            
            // add order message to queue
            pthread_mutex_lock(&mutexA);
            deliver_q.push(make_pair(-message_deliver_order_received, make_pair(sender_id_int, sender_message_count_int)));
            pthread_mutex_unlock(&mutexA);
        }
    }
    close(sockfd);
    return 1;
}

/* for total ordering, deliver message 
 1. check if order message queue is empty or not
 2. if not empty, then check if the smallest order is the same as this proc's own order number
 3. if true, then try to find order number corresponded message, and deliver it
 4. repeat step 1-3 until order message queue is empty or conditions are not true
 */
void deliverTotal()
{
    //    printf("calling deliverTotal ...\n");
    pthread_mutex_lock(&mutexA);
    //    printf("message_deliver_order %d, deliver_q size %d\n", message_deliver_order, deliver_q.size());
    while (!deliver_q.empty())
    {
        //        printf("received order %d, my order %d\n", -deliver_q.top().first, message_deliver_order);
        if (-deliver_q.top().first == message_deliver_order)
        {
            int sender_id = deliver_q.top().second.first;
            int sender_message_count = deliver_q.top().second.second;
            if (holdback_total[sender_id-1].find(sender_message_count) != holdback_total[sender_id-1].end())
            {
                // delete message from order queue
                deliver_q.pop();
                // increment deliver orderr
                message_deliver_order++;
                string message = holdback_total[sender_id-1][sender_message_count];
                // delete message from holdback map
                holdback_total[sender_id-1].erase(sender_message_count);
                time_t cur_t = time(0);
                // deliver message
                printf("%s\n", ("Delivered message \"" + message + "\" from process " + to_string(sender_id) + ", message count is " + to_string(sender_message_count) + ", system time is " + to_string(cur_t)).c_str());
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
    pthread_mutex_unlock(&mutexA);
}

/* listening message from cur proc's own port */
void* listening(void* data)
{
    //    printf("calling listening ...\n");
    ListenData* listenD = (ListenData*)data;
    while (1)
    {
        unicast_receive(listenD->proc_arr[master_proc_id-1]);
    }
    return NULL;
}

/* for total ordering, try to deliver message every 10s */
void* delivering(void*)
{
    //    printf("calling delivering ...\n");
    while (1) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        deliverTotal();
    }
    return NULL;
}


/* for fifo ordering
 before this step, add a new <sender, message> into holdback_pq when a message arrives
 only need to recursion on one sender, since a new message from a sender will triggle
 all valid message to be received
 */
void holdbackDeliverFIFO(int sender)
{
    //    printf("calling holdbackDeliver ...\n");
    pair<int, string> priority_message = holdback_pq[sender].top();
    int s = - priority_message.first;
    string message = priority_message.second;
    int r = vec_clock[sender];
    // if message is valid
    if (s == r + 1)
    {
        // remove the message to be delivered
        holdback_pq[sender].pop();
        
        /* critical section, do not allow reading from vec_clock when writing */
        pthread_mutex_lock(&mutexA);
        // update vec_clock
        vec_clock[sender] = s;
        pthread_mutex_unlock(&mutexA);
        
        // deliver the message
        messageDeliverFIFO(message, sender);
        // recursively call deliver until holdback_pq is empty
        if (holdback_pq[sender].size() > 0)
        {
            holdbackDeliverFIFO(sender);
        }
    }
    // if message arrive too late, reject
    else if (s < r + 1)
    {
        holdback_pq[sender].pop(); // remove invalid message
        holdbackDeliverFIFO(sender);
    }
    else if (s > r + 1)
    {
        // do nothing, wait until the valid message arrives
    }
}
/* dliver message fro fifo ordering */
void messageDeliverFIFO(string& message, int sender_id)
{
    time_t cur_t = time(0);
    printf("%s\n", ("Received \"" + message + "\" from process " + to_string(sender_id+1) + ", message count " + ", system time is " + std::to_string(cur_t)).c_str());
}

/* for causal ordering, try to deliver message
 by comparing process's own vectorstamp and
 those that are piggyback through received messages
 and are pushed into holdback queue
 */
void holdbackDeliverCausal(int master_id, int sender_id)
{
    //    printf("calling holdbackDeliverCausal ..., sender %d, list size %d\n", sender_id-1, holdback_causal.size());
    time_t cur_t = time(0);
    list< pair< vector<int>, string > >::iterator itr;
    
    for (itr = holdback_causal.begin(); itr!=holdback_causal.end(); itr++)
    {
        int compare_val = compareVecClock(vec_clock, itr->first, sender_id);
        if ( compare_val == 1)
        {
            // update vector clock at sender
            vec_clock[sender_id-1] += 1;
            
            printf("%s\n", ("Delivered \"" + itr->second + "\" from process " + std::to_string(sender_id) + ", message count " + std::to_string(itr->first[sender_id-1]) + ", system time is " + std::to_string(cur_t)).c_str());
            // remove delivered message
            holdback_causal.erase(itr);
            /* recursively find next valid message, in case any 
             previous becomes valid after updating vector clock */
            if (holdback_causal.size() > 0)
            {
                for (int sender_id = 1; sender_id<=total_proc; sender_id++)
                {
                    holdbackDeliverCausal(master_id, sender_id);
                }
            }
        }
        else if (compare_val == 0)
        {
            // reject invalid message
            holdback_causal.erase(itr);
        }
        else if (compare_val == 2)
        {
            // do nothing, wait until the valid message arrives
        }
        
        if (holdback_causal.size() == 0)
            break;
    }
}

/*
 compare two vectorstamps
 return 0 if v2 < v1
 return 1 if v1 = v2
 return 2 if v2 > v1
 */
int compareVecClock(vector<int>& my_clock, vector<int>& received_clock, int sender_id)
{
    //    printf("calling compareVecClock ...\n");
    if (holdback_causal.empty()) return 2;
    
    /* print out compared two vectorstamps */
    //    for (auto x:my_clock) printf("%d ", x);
    //    printf("\n");
    
    //    for (auto x:received_clock) printf("%d ", x);
    //    printf("\n");
    
    if (my_clock[sender_id-1] +1 == received_clock[sender_id-1])
    {
        for (int i = 0; i<my_clock.size(); i++)
        {
            if (i != sender_id-1)
            {
                // holdback
                if (received_clock[i] > my_clock[i])
                {
                    return 2;
                }
            }
        }
        // deliver
        return 1;
    }
    
    return 2; // holdback
}



int main(int argc, char *argv[])
{
    /* input exception */
    if (argc != 4)
    {
        fprintf(stderr,"usage: mp1 config procID multiCastType\n");
        exit(1);
    }
    /* read cur proc id */
    master_proc_id = atoi(argv[2]);
    printf("cur process is %d\n", master_proc_id);
    
    /* read in config */
    ifstream CONFIG(argv[1]);
    string range;
    getline(CONFIG, range);
    istringstream split_range(range);
    int max;
    split_range >> min >> max;
    delay_range = max - min;
    
    vector<string> procs;
    while (CONFIG)
    {
        string line;
        getline(CONFIG, line);
        if (line.length() > 1) // skip empty line
            procs.push_back(line);
    }
    total_proc = procs.size();
    
    // init vec_clock with 0s and holdback_pq arr
    for (int i = 0; i<total_proc;i++)
    {
        vec_clock.push_back(0);
        priority_queue< pair<int, string> > tmp_pq;
        holdback_pq.push_back(tmp_pq);
        unordered_map<int, string> tmp_map;
        holdback_total.push_back(tmp_map);
    }
    
    //    struct MultiCastProc* new_proc = new MultiCastProc [total_proc];
    for (int i = 0;i < procs.size();i++)
    {
        istringstream split_line(procs[i]);
        int proc_id;
        string proc_ip;
        string proc_port;
        split_line >> proc_id >> proc_ip >> proc_port;
        struct MultiCastProc* tmp_proc = new struct MultiCastProc;
        tmp_proc->proc_id = proc_id;
        tmp_proc->proc_ip = proc_ip;
        tmp_proc->proc_port = proc_port;
        proc_info.push_back(tmp_proc);
    }
    
    /* get the ordering of message deliver */
    if (strcmp(argv[3], "causal") == 0)
    {
        printf("causal ordering\n");
        ordering = "causal";
    }
    else if (strcmp(argv[3], "fifo") == 0)
    {
        printf("fifo ordering\n");
        ordering = "fifo";
    }
    else if (strcmp(argv[3], "total") == 0)
    {
        printf("total ordering\n");
        ordering = "total";
    }
    
    ListenData* listenD = new ListenData;
    listenD->proc_arr = proc_info;
    listenD->master_proc = master_proc_id;
    /* use a thread to listen to messages */
    pthread_t p_listen;
    pthread_create(&p_listen, NULL, listening, (void*)listenD);
    
    pthread_t p_deliver;
    if (ordering.compare("total") == 0 && master_proc_id != 1)
    {
        pthread_create(&p_deliver, NULL, delivering, NULL);
    }
    
    
    /* send to other proc's ports */
    srand(time(NULL));
    while (1)
    {
        string command;
        getline(cin, command);
        istringstream split_command(command);
        // command format
        string token1, dest, message;
        /* extract command variables */
        split_command >> token1;
        // exit chat room
        if (token1.compare("exit") == 0)
        {
            pthread_cancel(p_listen);
            break;
        }
        // multicast messages
        else if (token1.compare("msend") == 0)
        {
            message = split_command.str().substr(token1.length()+1);
            
            // lock when updating number of sent messages
            pthread_mutex_lock(&mutexA);
            // increment #sent message
            message_sent++;
            pthread_mutex_unlock(&mutexA);
            
            // multicast
            msend(message);
        }
        // handle exceptions
        else
        {
            printf("message not understood, please try again!\n");
        }
    }
    
    return 0;
}
