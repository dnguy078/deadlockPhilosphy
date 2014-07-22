/*
User: Dennis Nguyen
netid: dnguy078
Programming Assignment 6

*/
#include <map>  
#include <pthread.h>
#include <semaphore.h>
#include <cassert>
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <queue>
#include <iomanip>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>

using namespace std;

template< typename T >
inline string T2a( T x ) { ostringstream s; s<<x; return s.str(); }
template< typename T >
inline string id( T x ) { return T2a( (unsigned long) x ); }

#define cdbg cerr << "\nLn " << __LINE__ << " of " << setw(8) << __FUNCTION__ << " by " << report() 

#define cdbg2 cout << "\nLn " << __LINE__ << " of " << setw(8) << __FUNCTION__ << " by " << report() 
#define EXCLUSION Sentry exclusion(this); exclusion.touch();
class Thread;
extern string Him(Thread*);
extern string Me();
extern string report( );  
class Philosopher; 
class Chopstick; 

// ====================== priority aueue ============================

// pQueue behaves exactly like STL's queue, except that push has an
// optional integer priority argument that defaults to INT_MAX.
// front() returns a reference to the item with highest priority
// (i.e., smallest priority number) with ties broken on a FIFO basis.
// It may overflow and malfunction after INT_MAX pushes.

template<class T>                // needed for priority comparisions
bool operator<( const pair<pair<int,int>,T>& a, 
                const pair<pair<int,int>,T>& b  
              ) {
  return a.first.first > b.first.first ? true  :
         a.first.first < b.first.first ? false :
               a.first.second > b.first.second ;
} 

template <class T>
class pQueue {
  priority_queue< pair<pair<int,int>,T> > q;  
  int i;  // counts invocations of push to break ties on FIFO basis.
public:
  pQueue() : i(0) {;}
  void push( T t, int n = INT_MAX ) { 
    q.push( pair<pair<int,int>,T>(pair<int,int>(n,++i),t) ); 
  }
  void pop() { q.pop(); }
  T front() { return q.top().second; }
  int size() { return q.size(); }
  bool empty() { return q.empty(); }
};

// =========================== interrupts ======================

class InterruptSystem {
public:       // man sigsetops for details on signal operations.
  static void handler(int sig);
  // exported pseudo constants
  static sigset_t on;                    
  static sigset_t alrmoff;    
  static sigset_t alloff;     
  InterruptSystem() {  
    signal( SIGALRM, InterruptSystem::handler ); 
    sigemptyset( &on );                    // none gets blocked.
    sigfillset( &alloff );                  // all gets blocked.
    sigdelset( &alloff, SIGINT );
    sigemptyset( &alrmoff );      
    sigaddset( &alrmoff, SIGALRM ); //only SIGALRM gets blocked.
    set( alloff );        // the set() service is defined below.
    struct itimerval time;
    time.it_interval.tv_sec  = 0;
    time.it_interval.tv_usec = 400000;
    time.it_value.tv_sec     = 0;
    time.it_value.tv_usec    = 400000;
    cerr << "\nstarting timer\n";
    setitimer(ITIMER_REAL, &time, NULL);
  }
  sigset_t set( sigset_t mask ) {
    sigset_t oldstatus;
    pthread_sigmask( SIG_SETMASK, &mask, &oldstatus );
    return oldstatus;
  } // sets signal/interrupt blockage and returns former status.
  sigset_t block( sigset_t mask ) {
    sigset_t oldstatus;
    pthread_sigmask( SIG_BLOCK, &mask, &oldstatus );
    return oldstatus;
  } //like set but leaves blocked those signals already blocked.
};

// signal mask pseudo constants
sigset_t InterruptSystem::on;   
sigset_t InterruptSystem::alrmoff; 
sigset_t InterruptSystem::alloff;  

InterruptSystem interrupts;               // singleton instance.


// ========= Threads, Semaphores, Sentry's, Monitors ===========

class Semaphore {           // C++ wrapper for Posix semaphores.
  sem_t s;                               // the Posix semaphore.
public:
  Semaphore( int n = 0 ) { assert( ! sem_init(&s, 0, n) ); }
  ~Semaphore()           { assert( ! sem_destroy(&s) );    }
  void release()         { assert( ! sem_post(&s) );       }
  void acquire() {
    sigset_t old = interrupts.set( InterruptSystem::alloff );
    sem_wait( &s );
    interrupts.set( old );
  }
};


class Lock : public Semaphore {
public:             // A Semaphore initialized to one is a Lock.
  Lock() : Semaphore(1) {} 
};


class Monitor : Lock {
  friend class Sentry;
  friend class Condition;
  sigset_t mask;
public:
  void lock()   { Lock::acquire(); }
  void unlock() { Lock::release(); }
  Monitor( sigset_t mask = InterruptSystem::alrmoff ) 
    : mask(mask) 
  {}
};


class Sentry {            // An autoreleaser for monitor's lock.
  Monitor&  mon;           // Reference to local monitor, *this.
  const sigset_t old;    // Place to store old interrupt status.
  bool sentrysupress; 
public:
  void touch() {}          // To avoid unused-variable warnings.
  Sentry( Monitor* m ) 
    : mon( *m ), 
      old( interrupts.block(mon.mask) )
  { 
  	sentrysupress = false; 
    mon.lock(); 
  }
  ~Sentry() {
  	if (!sentrysupress)
    	mon.unlock();
    interrupts.set( old ); 
  }
  void suppress_unlock(){
  	sentrysupress = true; 
  }
  
};


template< typename T1, typename T2 >
class ThreadSafeMap : Monitor {
  map<T1,T2> m;
public:
  T2& operator [] ( T1 x ) {
    EXCLUSION
    return m[x];
  }
};


class Thread {
  friend class Condition;
  friend class CPUallocator;                      // NOTE: added.
  pthread_t pt;                                    // pthread ID.
  static void* start( Thread* );
  virtual void action() = 0;
  Semaphore go;
  static ThreadSafeMap<pthread_t,Thread*> whoami;  

  virtual int priority() { 
    return INT_MAX;         // place holder for complex policies.
  }   

  void suspend() { 
    cdbg << "Suspending thread \n";
    go.acquire(); 
    cdbg << "Unsuspended thread \n";
  }

  void resume() { 
    cdbg << "Resuming \n";
    go.release(); 
  }

  int self() { return pthread_self(); }

  void join() { assert( pthread_join(pt, NULL) ); }

public:

  string name; 

  static Thread* me();

  virtual ~Thread() { pthread_cancel(pt); }  

  Thread( string name = "" ) 
    : name(name)
  {
    cerr << "\ncreating thread " << Him(this) << endl;
    assert( ! pthread_create(&pt,NULL,(void*(*)(void*))start,this));
  }

};


class Condition : pQueue< Thread* > {
  Monitor&  mon;                     // reference to local monitor
public:
  Condition( Monitor* m ) : mon( *m ) {;}
  int waiting() { return size(); }
  bool awaited() { return waiting() > 0; }
  void wait( int pr = INT_MAX );    // wait() is defined after CPU
  void tail_wait( int pr = INT_MAX );    // wait() is defined after CPU
  void signal() { 
    if ( awaited() ) {
      Thread* t = front();
      pop();
      cdbg << "Signalling" << endl;
      t->resume();
    }
  }
  void broadcast() { while ( awaited() ) signal(); }
}; 


// ====================== AlarmClock ===========================

class AlarmClock : Monitor {
  unsigned long now;
  unsigned long alarm;
  Condition wakeup;
public:
  AlarmClock() 
    : now(0),
      alarm(INT_MAX),
      wakeup(this)
  {;}
  int gettime() { EXCLUSION return now; }
  void wakeme_at(int myTime) {
    EXCLUSION
    if ( now >= myTime ) return;  // don't wait
    if ( myTime < alarm ) alarm = myTime;      // alarm min= myTime
    while ( now < myTime ) {
      cdbg << " ->wakeup wait " << endl;
      wakeup.wait(myTime);
      cdbg << " wakeup-> " << endl;
      if ( alarm < myTime ) alarm = myTime;
    }
    alarm = INT_MAX;
    wakeup.signal();
  }
  void wakeme_in(int time) { wakeme_at(now+time); }
  void tick() { 
    EXCLUSION
    cdbg << "now=" << now << " alarm=" << alarm
         << " sleepers=" << wakeup.waiting() << endl;
    if ( now++ >= alarm ) wakeup.signal();
  }
};

extern AlarmClock dispatcher;                  // singleton instance.


class Idler : Thread {                       // awakens periodically.
  // Idlers wake up periodically and then go back to sleep.
  string name;
  int period;
  int priority () { return 0; }                  // highest priority.
  void action() { 
    cdbg << "Idler running\n";
    int n = 0;
    for(;;) { 
      cdbg << "calling wakeme_at\n";
      dispatcher.wakeme_at( ++n * period ); 
    } 
  }
public:
  Idler( string name, int period = 5 )    // period defaults to five.
    : Thread(name), period(period)
  {}
};


//==================== CPU-related stuff ========================== 




class CPUallocator : Monitor {                            
  // a scheduler for the pool of CPUs or anything else.  (In reality
  // we are imlementing a prioritized semaphore.)

  friend string report();
  pQueue<Thread*> ready;       // queue of Threads waiting for a CPU.
  int cpu_count;           // count of unallocated (i.e., free) CPUs.

public:

  CPUallocator( int n ) : cpu_count(n) {;}

  void release()  {
    EXCLUSION
    // Return your host to the pool by incrementing cpu_count.
    // Awaken the highest priority sleeper on the ready queue.
	cpu_count++;
	if (!ready.empty())
	{		
		Thread* front = ready.front(); 
		
		ready.pop(); 
		front->resume(); 

	}
	//return; 
    // Fill in your code.

  }

  void acquire( int pr = Thread::me()->priority() ) {
    EXCLUSION
	while (cpu_count==0)
	{
		ready.push(Thread::me(), pr); 
		Monitor::unlock(); 
		Thread::me()->suspend(); 
		Monitor::lock();  
	}
		cpu_count--;
    // While the cpu_count is zero, repeatedly, put yourself to 
    // sleep on the ready queue at priority pr.  When you finally 
    // find the cpu_count greater than zero, decrement it and 
    // proceed.
    // 
    // Always unlock the monitor the surrounding Monitor before 
    // going to sleep and relock it upon awakening.  There's no
    // need to restore the preemption mask upon going to sleep; 
    // this host's preemption mask will automatically get reset to 
    // that of its next guest.

    // Fill in your code here.

  }
  /*
  void defer( int pr = Thread::me()->priority() ) {
    release(); 
	sleep(1); 
    acquire(pr); 
    // This code works but is a prototype for testing purposes only:
    //  * It unlocks the monitor and restores preemptive services
    //    at the end of release, only to undo that at the beginning 
    //    of acquire.
    //  * It gives up the CPU even if this thread has the higher 
    //    priority than any of the threads waiting on ready.
  }
  */
  void defer( int pr = Thread::me()->priority() ) {
    EXCLUSION
    if ( ready.empty() ) return;
	ready.push(Thread::me(), pr);
	Thread* highThread = ready.front(); 
	if (highThread == Thread::me())
	{
		ready.pop(); 
		return;
	}
	else
	{
		cpu_count++;
		ready.pop();
		highThread->resume(); 
		Monitor::unlock(); 
		Thread::me()->suspend(); 
		Monitor::lock();  
			while (cpu_count==0)
			{

				ready.push(Thread::me(), pr); 
				Monitor::unlock(); 
				Thread::me()->suspend(); 
				Monitor::lock();  
			}
			cpu_count--;

		}

    //  Put yourself onto the ready queue at priority pr.  Then,
    //  extract the highest priority thread from the ready queue.  If
    //  that thread is you, return.  Otherwise, put yourself to sleep.
    //  Then proceed as in acquire(): While the cpu_count is zero,
    //  repeatedly, put yourself to sleep on the ready queue at
    //  priority pr.  When you finally find the cpu_count greater than
    //  zero, decrement it and proceed.

    // Fill in your code.

  }
}; 



/*
class CPUallocator {
  // a scheduler for the pool of CPUs.
  Semaphore sem;         // a semaphore appearing as a pool of CPUs.
public:
  int cpu_count;  // not used here and set to visibly invalid value.
  pQueue<Thread*> ready;
  CPUallocator( int count ) : sem(count), cpu_count(-1) {}
  void defer() { sem.release(); sem.acquire(); }
  void acquire( int pr = INT_MAX ) { sem.acquire(); }
  void release() { sem.release(); }
};
*/

extern CPUallocator CPU;  // single instance, init declaration here.


void InterruptSystem::handler(int sig) {                  // static.
  static int tickcount = 0;
  cdbg << "TICK " << tickcount << endl; 
  dispatcher.tick(); 
  if ( ! ( (tickcount++) % 3 ) ) {
    cdbg << "DEFERRING \n"; 
    CPU.defer();                              // timeslice: 3 ticks.
  }
  assert( tickcount < 35 );
} 


void* Thread::start(Thread* myself) {                     // static.
  interrupts.set(InterruptSystem::alloff);
  cerr << "\nStarting thread " << Him(myself)  // cdbg crashes here.
       << " pt=" << id(pthread_self()) << endl; 
  assert( myself );
  whoami[ pthread_self() ] = myself;
  assert ( Thread::me() == myself );
  interrupts.set(InterruptSystem::on);
  cdbg << "waiting for my first CPU ...\n";
  CPU.acquire();  
  cdbg << "got my first CPU\n";
  myself->action();
  cdbg << "exiting and releasing cpu.\n";
  CPU.release();
  pthread_exit(NULL);   
}
void Condition::tail_wait(int pr)
{
   push( Thread::me(), pr );
 	 cdbg << "releasing CPU just prior to wait.\n";
 	 mon.unlock();
 	 CPU.release();  
 	 cdbg << "WAITING\n";
   Thread::me()->suspend();
 	 CPU.acquire();  
}
void Condition::wait( int pr ) {
  push( Thread::me(), pr );
  cdbg << "releasing CPU just prior to wait.\n";
  mon.unlock();
  CPU.release();  
  cdbg << "WAITING\n";
  Thread::me()->suspend();
  CPU.acquire();  
  mon.lock(); 
}


// ================ application stuff  ==========================

class InterruptCatcher : Thread {
public:
  // A thread to catch interrupts, when no other thread will.
  int priority () { return 1; }       // second highest priority.
  void action() { 
    cdbg << "now running\n";
    for(;;) {
      CPU.release();         
      pause(); 
      cdbg << " requesting CPU.\n";
      CPU.acquire();
    }
  }
public:
  InterruptCatcher( string name ) : Thread(name) {;}
};


class Pauser {                            // none created so far.
public:
  Pauser() { pause(); }
};



// ================== threads.cc ===================================

// Single-instance globals

ThreadSafeMap<pthread_t,Thread*> Thread::whoami;         // static
Idler idler(" Idler ");                        // single instance.
InterruptCatcher theInterruptCatcher("IntCatcher");  // singleton.
AlarmClock dispatcher;                         // single instance.
CPUallocator CPU(1);                 // single instance, set here.
string Him( Thread* t ) { 
  string s = t->name;
  return s == "" ? id(t) : s ; 
}
Thread* Thread::me() { return whoami[pthread_self()]; }  // static
string Me() { return Him(Thread::me()); }

               // NOTE: Thread::start() is defined after class CPU


// ================== diagnostic functions =======================

string report() {               
  // diagnostic report on number of unassigned cpus, number of 
  // threads waiting for cpus, and which thread is first in line.
  ostringstream s;
  s << Me() << "/" << CPU.cpu_count << "/" << CPU.ready.size() << "/" 
    << (CPU.ready.empty() ? "none" : Him(CPU.ready.front())) << ": ";
  return s.str(); 
}


// =================== main.cc ======================================

//
// main.cc 
//

// applied monitors and thread classes for testing

class SharableInteger: public Monitor {
public:
  int data;
  SharableInteger() : data(0) {;}
  void increment() {
    EXCLUSION
    ++data;
  }
  string show() {
    EXCLUSION
    return T2a(data);
  }  
} counter;                                        // single instance


class Incrementer : Thread {
  int priority() { return INT_MAX - 1; }            // high priority
  void action() { 
    cdbg << "New incrementer running.\n";
    for(int i= 0; i < 120; ++i) {
      for(int i=0;i<12000000;++i) {}  // delay
      counter.increment();
      cdbg << " Counter : " << counter.show() << endl;
    }
    cdbg << Me() << " done\n";
  }
public:
  Incrementer( string name ) : Thread(name) {;}
};


// ======= control order of construction of initial threads  ========


// Create and run three concurrent Incrementers for the single 
// global SharableInteger, counter, as a test case.


vector<Chopstick*> chop;
vector<Philosopher*> phil;


 void mydelay(int n ) 
 {
 usleep(1000*n); 
}
typedef enum {red, green, yellow} colors;
class AllocationGraph : Monitor
{
    map< Thread*, map<Chopstick*, string> > g;
public:
    void insert(Thread* t, Chopstick* c, string s)
    {
        EXCLUSION
        g[t][c] = s;   
    }
    void report(int n);
};
AllocationGraph theGraph;

Semaphore thestart;

class Chopstick : Monitor {
public:
  colors color;
  bool held;
  Condition available;
  
  
  Chopstick( colors color )
    : color(color), held(false), available(this) {}
  void release()
  {
      EXCLUSION
      theGraph.insert(Thread::me(), this, "");
      held = false;
      available.signal();
  }
  void acquire()
  {
      EXCLUSION
      theGraph.insert(Thread::me(), this, "awaited");
      while(held)
      {
          available.wait();
      }
      theGraph.insert(Thread::me(), this, "acquired");
      held = true;
  }
};

inline int myrand() { // a simple random number generator
	static unsigned long next = 3;
	next = next * 1103515245 + 12345;
	unsigned x = (unsigned) (next/65536) % 32768 ;
	return x % 99;
}
 
class Philosopher : public Thread {
private:
  Chopstick* leftstick;
  Chopstick* rightstick;
  int id;
  int n;
  vector<Chopstick*> chopstick_set;
public:
  string state;
  Philosopher( int id, int n )
    : Thread("Phil #" + T2a(id)), id(id), n(n)
  {
    state = "p";
  }
  
  void action()
  {
      thestart.acquire();
      thestart.release();
      leftstick = chop[id];
      rightstick = chop[(id + 1) % n];
      for(int i = 0; i != 120; ++i)
      {
        state = "p";
        mydelay(myrand());
        map<colors, Chopstick*> stick;
        stick[leftstick->color] = leftstick;
        stick[rightstick->color] = rightstick;
        Chopstick* firststick = stick[red] ? stick[red] : stick[yellow];
        Chopstick* secondstick = stick[green] ? stick[green] : stick[yellow];
        
        state = "w";
        firststick->acquire();
        secondstick->acquire();
        
        state = "e";
        mydelay(myrand());
        colors c = leftstick->color;
        if(c == yellow) swap(leftstick->color, rightstick->color);
        if(leftstick->color == green )
        {
            leftstick->release();
            rightstick->release();
        }
        else
        {
            rightstick->release();
            leftstick->release();
        }
      }
  }
};




void AllocationGraph::report(int n)
{
    thestart.release(); 
      map<int,int> p;
    map<int,int> w;
    map<int,int> e;

    for(int j = 0; j < 2000; ++j)
    {
        mydelay(100);
        EXCLUSION
        for ( int i = 0; i != n; ++ i )
        {
            if(phil[i]->state == "p") ++p[i];
            else if(phil[i]->state == "w") ++w[i];
            else if(phil[i]->state == "e") ++e[i];
            else exit(0);
            map<colors, string> colorcode;
            colorcode[red] = "red";
            colorcode[green] = "green";
            colorcode[yellow] = "yellow";
            cout << " "
            << colorcode[ chop[i]->color ]
            << endl;
            string s = theGraph.g[ phil[i] ][ chop[i] ];
            cout << ( s == "awaited" ? " ^\n /\n" :
            s == "acquired" ? " /\n V\n" :
            "\n\n" );
            s = theGraph.g[ phil[i] ][ chop[(i+1)%n] ];
            int sum = p[i]+w[i]+e[i];
            cout << phil[i]->name
            << " P:" << p[i]*100/sum
            << "% W:"<< w[i]*100/sum
            << "% E:"<< e[i]*100/sum
            << "%\n"
            << ( s == "acquired" ? " ^\n \\ \n" :
             "awaited" ? "\\ \n V\n" : "\n\n" );
        }
        cout << "=== " << j << " ==============================================\n";
    }
}

int main( int argc, char *argv[] )
{
	if(argc < 2) exit(0);
    int n = atoi(argv[1]);
 
    interrupts.set(InterruptSystem::alloff);
    colors color;
    for(int i = 0; i != n; ++i)
    {
        if(n % 2 != 0 && i == 0) color = yellow;
        else if(i % 2 == 0) color = green;
        else color = red;
        chop.push_back(new Chopstick(color));
        
    }
    for(int i = 0; i != n; ++i)
    {
        phil.push_back(new Philosopher(i, n));
    }
    theGraph.report(n);
}
