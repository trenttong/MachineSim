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

UINT32 *critsecLevel = NULL;

/// signature int pthread_mutex_lock(pthread_mutex_t *mutex);
static int PthreadMutexLockWrapper(CONTEXT* ctxt, AFUNPTR origFptr, pthread_mutex_t* mtx)
{
    int retcode;
    /* call the application clSetKernelArg */
    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(),
                                CALLINGSTD_DEFAULT, origFptr,
                                PIN_PARG(int), &retcode,
                                PIN_PARG(pthread_mutex_t*), mtx,
                                PIN_PARG_END());

    if (!retcode)
    {
        critsecLevel[PIN_ThreadId()] ++;
        simglobals->get_global_simlog()->logme(SIMLOG::SUPERVERBOSE, "thread %d entering CS level %d", 
                                               PIN_ThreadId(), critsecLevel[PIN_ThreadId()]);
    }

    return retcode;
}

static int PthreadMutexUnlockWrapper(CONTEXT* ctxt, AFUNPTR origFptr, pthread_mutex_t* mtx)
{
    int retcode;
    /* call the application clSetKernelArg */
    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(),
                                CALLINGSTD_DEFAULT, origFptr,
                                PIN_PARG(int), &retcode,
                                PIN_PARG(pthread_mutex_t*), mtx,
                                PIN_PARG_END());

    if (!retcode)
    {
        critsecLevel[PIN_ThreadId()] --;
        simglobals->get_global_simlog()->logme(SIMLOG::SUPERVERBOSE, "thread %d leaving CS level %d", 
                                               PIN_ThreadId(), critsecLevel[PIN_ThreadId()]);
    }
    return retcode;
}

static int PthreadCreateWrapper(CONTEXT* ctxt, AFUNPTR origFptr, pthread_t *thrd, 
                                const pthread_attr_t *attr, void *sroutine, void *arg)
{
    int retcode;
    /* call the application clSetKernelArg */
    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(),
                                CALLINGSTD_DEFAULT, origFptr,
                                PIN_PARG(int), &retcode,
                                PIN_PARG(pthread_t*), thrd,
                                PIN_PARG(const pthread_attr_t*), attr,
                                PIN_PARG(void*), sroutine,
                                PIN_PARG(void*), arg,
                                PIN_PARG_END());

    if (!retcode)
    {
        static UINT32 workernum = 0;
        simglobals->get_global_simlowl()->atom_int32_inc((INT*)&workernum);
        simglobals->get_global_simlog()->logme(SIMLOG::SUPERVERBOSE, "thread %d created", workernum);

        /// expected number of threads created.
        if (workernum >= simopts->get_workercount()) simwait->clrwait(SIMPARAMS::WAIT_WORKER_THREAD);
    }
    return retcode;
}

VOID ImageInstrument(IMG img, VOID *v)
{
    /// instrumented function.
    /// name: pthread_mutex_lock
    /// signature: int pthread_mutex_lock(pthread_mutex_t *mutex);
    const std::string pthreadMutexLockFunname = "pthread_mutex_lock";
    RTN plockRtn = RTN_FindByName(img, pthreadMutexLockFunname.c_str());
    if (RTN_Valid(plockRtn))
    {
        PROTO protoPlock = PROTO_Allocate(PIN_PARG(int), CALLINGSTD_DEFAULT,
                                          pthreadMutexLockFunname.c_str(),
                                          PIN_PARG(pthread_mutex_t*),
                                          PIN_PARG_END());

        RTN_ReplaceSignature(plockRtn, AFUNPTR(PthreadMutexLockWrapper),
                             IARG_PROTOTYPE, protoPlock,
                             IARG_CONTEXT,
                             IARG_ORIG_FUNCPTR,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_END);

    }

    /// instrumented function.
    /// name: pthread_mutex_unlock
    /// signature: int pthread_mutex_lock(pthread_mutex_t *mutex);
    const std::string pthreadMutexUnlockFunname = "pthread_mutex_unlock";
    RTN punlockRtn = RTN_FindByName(img, pthreadMutexUnlockFunname.c_str());
    if (RTN_Valid(punlockRtn))
    {
        PROTO protoPunlock = PROTO_Allocate(PIN_PARG(int), CALLINGSTD_DEFAULT,
                                            pthreadMutexUnlockFunname.c_str(),
                                            PIN_PARG(pthread_mutex_t*),
                                            PIN_PARG_END());

        RTN_ReplaceSignature(punlockRtn, AFUNPTR(PthreadMutexUnlockWrapper),
                             IARG_PROTOTYPE, protoPunlock,
                             IARG_CONTEXT,
                             IARG_ORIG_FUNCPTR,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_END);

    }

    /// instrumented function.
    /// name: pthread_create
    /// signature: int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    ///                               void *(*start_routine) (void *), void *arg);
    const std::string pthreadCreateFunname = "pthread_create";
    RTN pcreateRtn = RTN_FindByName(img, pthreadCreateFunname.c_str());
    if (RTN_Valid(pcreateRtn))
    {
        PROTO protoPcreate = PROTO_Allocate(PIN_PARG(int), CALLINGSTD_DEFAULT,
                                            pthreadCreateFunname.c_str(),
                                            PIN_PARG(pthread_t*),
                                            PIN_PARG(pthread_attr_t*),
                                            PIN_PARG(void*),
                                            PIN_PARG(void*),
                                            PIN_PARG_END());

        RTN_ReplaceSignature(pcreateRtn, AFUNPTR(PthreadCreateWrapper),
                             IARG_PROTOTYPE, protoPcreate,
                             IARG_CONTEXT,
                             IARG_ORIG_FUNCPTR,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                             IARG_END);

    }
}

