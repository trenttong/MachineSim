/*BEGIN_LEGAL
Intel Open Source License

Copyright (c) 2002-2011 Intel CorpORAtion. All rights reserved.

Written by Xin Tong, University of Toronto.

Redistribution and use in source and binary fORMs, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary fORM must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel CorpORAtion nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

/* ===================================================================== */
/* This file contains an PIN tool for image leve instrumentation         */
/* ===================================================================== */

#include "pin.H"
#include "utils.hh"

UINT64 mmufault;

static void QEMUTlbRefillWrapper(CONTEXT* ctxt, AFUNPTR origFptr, void *env, 
                                 unsigned long long addr, int is_write, 
                                 int mmu_idx, uintptr_t retaddr)
{
    int retcode;
    /* call the application clSetKernelArg */
    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(),
                                CALLINGSTD_DEFAULT, origFptr,
                                PIN_PARG(int), &retcode,
                                PIN_PARG(void*), env,
                                PIN_PARG(unsigned long long), addr,
                                PIN_PARG(int), is_write,
                                PIN_PARG(int), mmu_idx,
                                PIN_PARG(uintptr_t), retaddr,
                                PIN_PARG_END());
    mmufault ++;
}

VOID RoutineInstrument(RTN rtn, VOID *)
{
    /// instrumented function.
    /// name: pthread_mutex_lock
    /// signature: int pthread_mutex_lock(pthread_mutex_t *mutex);
    const std::string QEMUTlbRefillFunName = "cpu_x86_handle_mmu_fault";
    if (RTN_Name(rtn) == QEMUTlbRefillFunName)
    {
        PROTO protoTLB = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT,
                                        QEMUTlbRefillFunName.c_str(),
                                        PIN_PARG(void*),
                                        PIN_PARG(unsigned long long), 
                                        PIN_PARG(int), 
                                        PIN_PARG(int),
                                        PIN_PARG(uintptr_t),
                                        PIN_PARG_END());

        RTN_ReplaceSignature(rtn, AFUNPTR(QEMUTlbRefillWrapper),
                             IARG_PROTOTYPE, protoTLB,
                             IARG_CONTEXT,
                             IARG_ORIG_FUNCPTR,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 4, 
                             IARG_END);

    }
}


