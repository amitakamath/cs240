/**
 * Context switches to new_context from old_context.
 *
 * Saves the current execution context into the `Context` structure pointed to
 * by the first paramter and restores the execution context pointed to by the
 * second paramter. As such, when this function returns, it jumps to the address
 * at the top of the stack (pointed to by %rsp).
 *
 * This function has the following signature in C:
 * context_switch(Context* old_context, Context* new_context)
 */
.text
.globl context_switch
.align 16
context_switch:
  movq    %rsp, 0x0(%rdi)

  stmxcsr 0x38(%rdi)
  fnstcw  0x3C(%rdi)

  ldmxcsr 0x38(%rsi)
  fldcw   0x3C(%rsi)
  /* FIXME: Phase 2
   * Save and restore execution context.
   * Part of the code is provided.
   */
  
  /* We save the current registers
  * into the old context in %rdi 
  * and the move the new context
  * in %rsi into the corresponding
  * registers. We get the appropriate
  * offset from the Context structure
  * in choloros.h
  */
 
  /* Context switch for %r15 */
  movq %r15, %rax
  movq %rax, 0x8(%rdi)
  movq 0x8(%rsi),%r15

  /* Context switch for %r14 */
  movq %r14, %rax
  movq %rax, 0x10(%rdi)
  movq 0x10(%rsi),%r14

  /* Context switch for %r13 */
  movq %r13, %rax
  movq %rax, 0x18(%rdi)
  movq 0x18(%rsi),%r13

  /* Context switch for %r12 */
  movq %r12, %rax
  movq %rax, 0x20(%rdi)
  movq 0x20(%rsi),%r12

  /* Context switch for %rbx */
  movq %rbx, %rax
  movq %rax, 0x28(%rdi)
  movq 0x28(%rsi),%rbx
  
  /* Context switch for %rbp */
  movq %rbp, %rax
  movq %rax, 0x30(%rdi)
  movq 0x30(%rsi),%rbp

  /* Context switch for %rsp */
  movq %rsp, %rax
  movq %rax, 0x0(%rdi)
  movq 0x0(%rsi), %rsp

  ret


/**
 * Initial function implicitly executed by a thread.
 *
 * This function should be jumped into implicitly by the green threads library.
 * It expects a function pointer at the top of the stack, and its argument above
 * that. It sets up the arguments in the right registers accordingly, and call
 * `thread_entry` which will handle subsequent initialization.
 */
.globl start_thread
.align 16
start_thread:
  push    %rbp
  movq    %rsp, %rbp
  movq    0x8(%rbp), %rdi
  movq    0x10(%rbp), %rsi
  callq   thread_entry
  hlt
