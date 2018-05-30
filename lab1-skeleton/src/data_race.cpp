#include <chloros.h>                                                           
#include <atomic>                                                              
#include <thread>                                                              
#include <vector>                                                              
#include <iostream>
#include <fstream>
#include <mutex>     
using namespace std;
                          
/*
This code implements the DLCP data race
that is caught by the thread sanitizer
*/                                    

//We use 3 kernel threads here                               
constexpr int const kKernelThreads = 3;

// Global File Pointer to be used by 
// all kernel threads                                      

FILE * pFile = NULL; 

// mutex protecting the file pointer
std::mutex mtx;                    

// The function called by the user thread
// spawned by the call chloros::Spawn()
void GreenThreadWorker(void*) { 
  // Check if the file pointer has been set
  // and return from the function if it is
  if(pFile == NULL)
  {
   // If the file pointer was NULL, acquire
   // a lock 
   mtx.lock();
   // Check that no other thread has set the
   // file pointer
   if(pFile == NULL) {
     // Set the file pointer
     pFile = fopen ("myfile.txt" , "w");
   }
   // Release the lock before returning 
   // from the function
   mtx.unlock();
  }
}

/*
The spawned kernel thread calls this function 
which in turn spawns a chloros
user thread using Spawn implemented
by our library. It waits for the thread
to return, prints its done and exists from
the funcion.
*/                                                                               
void KernelThreadWorker(int n) {                                               
  chloros::Initialize();                                                       
  chloros::Spawn(GreenThreadWorker, nullptr);                                  
  chloros::Wait();                                                             
  printf("Finished thread %d.\n", n);                                          
}                                                                              
 
/*
 Spawns three kernel threads and waits for
 them to return using threads.join() 
 */                                                                            
int main() {                                                                   
  std::vector<std::thread> threads{};                                          
  for (int i = 0; i < kKernelThreads; ++i) {                                   
    threads.emplace_back(KernelThreadWorker, i);                               
  }                                                                            
  for (int i = 0; i < kKernelThreads; ++i) {                                   
    threads[i].join();                                                         
  }                                                                            
  return 0;                                                                    
}           
