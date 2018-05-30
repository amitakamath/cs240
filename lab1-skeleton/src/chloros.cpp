#include "chloros.h"
#include "common.h"
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdlib.h>
#include <thread>
#include <vector>

extern "C" {

// Assembly code to switch context from `old_context` to `new_context`.
void ContextSwitch(chloros::Context* old_context,
                   chloros::Context* new_context) __asm__("context_switch");

// Assembly entry point for a thread that is spawn. It will fetch arguments on
// the stack and put them in the right registers. Then it will call
// `ThreadEntry` for further setup.
void StartThread(void* arg) __asm__("start_thread");
}

namespace chloros {

namespace {

// Default stack size is 2 MB.
constexpr int const kStackSize{1 << 21};

// Queue of threads that are not running.
std::vector<std::unique_ptr<Thread>> thread_queue{};

// Mutex protecting the queue, which will potentially be accessed by multiple
// kernel threads.
std::mutex queue_lock{};

// Current running thread. It is local to the kernel thread.
thread_local std::unique_ptr<Thread> current_thread{nullptr};

// Thread ID of initial thread. In extra credit phase, we want to switch back to
// the initial thread corresponding to the kernel thread. Otherwise there may be
// errors during thread cleanup. In other words, if the initial thread is
// spawned on this kernel thread, never switch it to another kernel thread!
// Think about how to achieve this in `Yield` and using this thread-local
// `initial_thread_id`.
thread_local uint64_t initial_thread_id;

}  // anonymous namespace

std::atomic<uint64_t> Thread::next_id;

Thread::Thread(bool create_stack)
    : id{0}, state{State::kWaiting}, context{}, stack{nullptr} {
  // FIXME: Phase 1
  /*
    In the constructor, we increment the next ID, set the state to
    kWaiting, and if the thread requires a stack, we allocate a stack
    to it using malloc(). In order to ensure that the stack is 16-byte
    aligned, we allocate slightly more memory than is required, then
    smash the allocated address (like NaCl) to the next highest 16-byte
    aligned address. We save these values to be used in the destructor.
    If no stack is to be allocated, the stack pointer is set to null.
  */
  //std::cout<<std::this_thread::get_id()<<std::endl;
  static_cast<void>(create_stack);
  id = next_id ++;
  state = State::kWaiting;
  if(create_stack) {
    uint8_t *ptr_init = (uint8_t *) malloc(kStackSize+16);
    uint8_t *ptr_stack = (uint8_t *)  (((uintptr_t )ptr_init+0x10) & ~(uintptr_t)0x0f);
    *(ptr_stack-1) = (ptr_stack-ptr_init);
    stack = ptr_stack;
  }
  else {
    stack = nullptr;
  }
  k_id = std::this_thread::get_id();
  // These two initial values are provided for you.
  context.mxcsr = 0x1F80;
  context.x87 = 0x037F;
}

Thread::~Thread() {
  // FIXME: Phase 1
  /*
    In the destructor function, we determine whether a stack 
    was allocated to this object: if it was, we determine the 
    address returned by malloc when the stack was initialized, 
    and we free that memory.
  */
  
  if(stack) {
    uint8_t *ptr_stack = stack;
    uint8_t *ptr_init = ptr_stack - *(ptr_stack-1);
    free(ptr_init);
  }

}

void Thread::PrintDebug() {
  fprintf(stderr, "Thread %" PRId64 ": ", id);
  switch (state) {
    case State::kWaiting:
      fprintf(stderr, "waiting");
      break;
    case State::kReady:
      fprintf(stderr, "ready");
      break;
    case State::kRunning:
      fprintf(stderr, "running");
      break;
    case State::kZombie:
      fprintf(stderr, "zombie");
      break;
    default:
      break;
  }
  fprintf(stderr, "\n\tStack: %p\n", stack);
  fprintf(stderr, "\tRSP: 0x%" PRIx64 "\n", context.rsp);
  fprintf(stderr, "\tR15: 0x%" PRIx64 "\n", context.r15);
  fprintf(stderr, "\tR14: 0x%" PRIx64 "\n", context.r14);
  fprintf(stderr, "\tR13: 0x%" PRIx64 "\n", context.r13);
  fprintf(stderr, "\tR12: 0x%" PRIx64 "\n", context.r13);
  fprintf(stderr, "\tRBX: 0x%" PRIx64 "\n", context.rbx);
  fprintf(stderr, "\tRBP: 0x%" PRIx64 "\n", context.rbp);
  fprintf(stderr, "\tMXCSR: 0x%x\n", context.mxcsr);
  fprintf(stderr, "\tx87: 0x%x\n", context.x87);
}

void Initialize() {
  auto new_thread = std::make_unique<Thread>(false);
  new_thread->state = Thread::State::kWaiting;
  initial_thread_id = new_thread->id;
  current_thread = std::move(new_thread);
}

void Spawn(Function fn, void* arg) {
  auto new_thread = std::make_unique<Thread>(true);
  // FIXME: Phase 3
  // Set up the initial stack, and put it in `thread_queue`. Must yield to it
  // afterwards. How do we make sure it's executed right away?

  /*
    In order to spawn a new thread, we create a new thread with a stack. Then
    we set its state to kReady, and set the stack to contain the StartThread
    pointer address at the TOS, then fn and then arg. The address of the 
    StartThread pointer is implicitly assigned to RIP, and fn and arg are
    taken as the first and second arguments passed to StartThread. The RSP is
    set to the TOS. A lock is obtained in order to add the new thread to the
    beginning of the thread queue, and is released before yielding.
  */

  static_cast<void>(fn);
  static_cast<void>(arg);
  static_cast<void>(StartThread);

  new_thread->state = Thread::State::kReady;

  uint64_t sthread_addr = (uint64_t)&StartThread;

  std::memcpy((void *)(new_thread->stack+(kStackSize - 24)),&sthread_addr,sizeof(sthread_addr));
  std::memcpy((void *)(new_thread->stack+(kStackSize - 16)),&fn,sizeof(fn));
  std::memcpy((void *)(new_thread->stack+(kStackSize - 8)),&arg,sizeof(arg));
   
  new_thread->context.rsp = (uint64_t)(new_thread->stack+(kStackSize - 24));
  queue_lock.lock();
  thread_queue.insert(thread_queue.begin(), std::move(new_thread));
  queue_lock.unlock();
  Yield(true);
  
}


bool Yield(bool only_ready) {

  /*
    In order to yield, check the queue to determine the next thread to yield 
    to. We then set the current thread to kReady if it is currently kRunning, 
    add it to the end of the queue, and remove the chosen thread to yield to 
    from the queue and set its state to kRunning. We context switch from the 
    old thread to the new one, and then call the garbage collector. We ensure
    that all accesses to the shared queue are done with locks, and that they
    are released at the appropriate time: ContextSwitch and GarbageCollect are
    part of the critical section, as described in the extra-credit write-up.
  */
  
  static_cast<void> (only_ready);
  int repl_value; 
  bool foundWaiting = false; 
  
  queue_lock.lock();
  
  for(unsigned int i=0;i<thread_queue.size();i++) {                                     
    if (thread_queue[i]->state == Thread::State::kReady || thread_queue[i]->state == Thread::State::kWaiting) {
      if (thread_queue[i]->id == initial_thread_id &&  thread_queue[i]->k_id != std::this_thread::get_id()) {
        // If the selected thread is the initial thread and the kernel thread currently running
        // isn't the kernel thread that initialized the selected (initial) thread, don't yield!
        // We modified the thread structure accordingly.
        continue;
      }
      if(only_ready && thread_queue[i]->state == Thread::State::kWaiting) {
        continue;
      }
      //else if (current_thread->id != initial_thread_id) {
      //  continue;
      //}
      else {                                                                       
        repl_value = i;                                                        
        foundWaiting = true;                                                   
        break;
      }                                                                 
    }                                                                        
  }     
   
  if(foundWaiting)
  {
    if (current_thread->state==Thread::State::kRunning) {                         
      current_thread->state = Thread::State::kReady;                             
    }                                                                           
    
    thread_queue.push_back(std::move(current_thread));                           
    thread_queue[repl_value]->state = Thread::State::kRunning;                   
    current_thread = std::move(thread_queue[repl_value]);                        
    thread_queue.erase(thread_queue.begin()+repl_value);                         
                                                                             
    Context* old_context = &thread_queue[thread_queue.size()-1]->context;       
    Context* new_context = &current_thread->context;                                                                         
    ContextSwitch(old_context, new_context);                        
                                                                               
    GarbageCollect();   
    queue_lock.unlock();
    return true;                         
  }
  
  queue_lock.unlock();
  return false;
}

void Wait() {
  current_thread->state = Thread::State::kWaiting;
  while (Yield(true)) {
    current_thread->state = Thread::State::kWaiting;
  }
}

void GarbageCollect() {
  /*
    In order to perform garbage collection, we iterate through the thread
    queue (note that this is part of the critical section, so a lock is
    held before entering this function), identify the threads in kZombie
    state, and remove them from the queue, freeing their allocated memory.
  */
  
  std::vector<std::unique_ptr<Thread>>::iterator iter = thread_queue.begin();
  while (iter != thread_queue.end()) {
    if((*iter)->state == Thread::State::kZombie) {
      Thread * temp = (*iter).release();
      delete temp;
      iter = thread_queue.erase(iter);
    }
    else {
      ++iter;
    }
  }
}

std::pair<int, int> GetThreadCount() {
  // Please don't modify this function.
  int ready = 0;
  int zombie = 0;
  
  std::lock_guard<std::mutex> lock{queue_lock};
  
  for (auto&& i : thread_queue) {
    if (i->state == Thread::State::kZombie) {
      ++zombie;
    } else {
      ++ready;
    }
  }
  return {ready, zombie};
}

void ThreadEntry(Function fn, void* arg) {
  /*
    We release the lock before calling fn. This is described
    in more detail in the extra credit write-up.
  */
  queue_lock.unlock();
  fn(arg);
  current_thread->state = Thread::State::kZombie;

  LOG_DEBUG("Thread %" PRId64 " exiting.", current_thread->id);
  // A thread that is spawn will always die yielding control to other threads.
  chloros::Yield();
  // Unreachable here. Why?
  ASSERT(false);
}

}  // namespace chloros
