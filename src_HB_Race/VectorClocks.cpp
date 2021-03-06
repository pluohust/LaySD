#include "VectorClocks.h"
#include "Init_Close.h"
#include "Detector.h"

/*please forgive my laziness.*/
extern map<UINT32, ThreadVecTime> AllThread;
extern map<THREADID, ThreadParent> ThreadMapParent;
extern map<OS_THREAD_ID, THREADID> ThreadIDInf;
extern PIN_LOCK lock;
extern PIN_LOCK shareaddrlock;
extern map<THREADID, RecordCond> VecWait;
extern map<THREADID, ADDRINT> AfterLockInf;
extern bool monitorendflag;
extern bool realendflag;
extern map<ADDRINT, map<UINT32, long> > SignalVecTime;
extern map<ADDRINT, map<UINT32, long> > LockVecTime;

extern map<UINT32, queue<map<UINT32, long> > > CreateThreadVecTime;
extern map<UINT32, map<UINT32, long> > FiniThreadVecTime;

extern int ThreadNum;
extern vector<ShareAddreeStruct> MallocVec;
extern size_t MaxSum;

extern ofstream RaceOut;

map<THREADID, ADDRINT> TryLockInf; // new add 

extern long LockNum; //lock count
extern long SynNum; //syn count

void InitThreadVecTime(UINT32 threadID)  //first frame when thread start
{
    map<UINT32, ThreadVecTime>::iterator ITforallthread;
    RWVecTime tmpRWVecTime;
    (tmpRWVecTime.VecTime)[threadID]=1;
    tmpRWVecTime.SynName="FirstFrame";

    ThreadVecTime tmpThreadVecTime;
    (tmpThreadVecTime.VecTimeList).push_back(tmpRWVecTime);

    AllThread.insert(pair<UINT32, ThreadVecTime>(threadID, tmpThreadVecTime));
    ITforallthread=AllThread.find(threadID);
    if(AllThread.end()==ITforallthread)
    {
        return;
    }
    
    (ITforallthread->second).ListAddress=((ITforallthread->second).VecTimeList).begin();

}

void CreateNewFrame(THREADID threadid, ThreadVecTime &TargetThread, string SynName)
{
    RWVecTime NewFrame;
    NewFrame.VecTime=(TargetThread.ListAddress)->VecTime;
    NewFrame.SynName=SynName;
    map<UINT32, long>::iterator mapvectime; //to find the thread's vec time
    mapvectime=(NewFrame.VecTime).find(threadid);
    if(mapvectime==(NewFrame.VecTime).end())
    {
        exit(-1);
    }
    mapvectime->second+=1;
    (TargetThread.VecTimeList).push_back(NewFrame);
    
    TargetThread.ListAddress=(TargetThread.VecTimeList).end();
    (TargetThread.ListAddress)--;
    if(TargetThread.ListAddress==(TargetThread.VecTimeList).end())
    {
        exit(-1);
    }
    list<struct RWVecTime>::iterator TestTheLastIterator;
    TestTheLastIterator=TargetThread.ListAddress;
    TestTheLastIterator++;
    if(TestTheLastIterator!=(TargetThread.VecTimeList).end())
    {
        exit(-1);
    }
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    RecordType GetOne;
    OS_THREAD_ID tmp;
    map<OS_THREAD_ID, THREADID>::iterator maptmp;
    struct ThreadVecTime OneThreadVecInf;
    set<ADDRINT>::iterator ITSharedVar;
    PIN_GetLock(&lock, threadid+1);
    ThreadNum++; //thread count
    if(INVALID_OS_THREAD_ID!=(tmp=PIN_GetParentTid()))
    {
        maptmp=ThreadIDInf.find(tmp);
        if(maptmp!=ThreadIDInf.end())
        {
            GetOne=(RecordType){1, threadid, (ADDRINT)maptmp->second};
            ThreadMapParent[threadid]=(ThreadParent) {true, maptmp->second}; //Just for thread join
        }
        else
        {
            GetOne=(RecordType){1, threadid, 0};
        }
    }
    else
    {
        GetOne=(RecordType){1, threadid, 0};
    }
    ThreadIDInf.insert(make_pair(PIN_GetTid(), threadid));
    
    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    if(AllThread.empty()) //first thread
    {
        InitThreadVecTime(GetOne.threadID);
    }
    else
    {
        list<struct RWVecTime>::iterator ITtmplist;
        for(ITforAllThread=AllThread.begin();ITforAllThread!=AllThread.end();ITforAllThread++)
        {
            for(ITtmplist=((ITforAllThread->second).VecTimeList).begin();ITtmplist!=((ITforAllThread->second).VecTimeList).end();ITtmplist++)
                (ITtmplist->VecTime).insert(pair<UINT32, long>(GetOne.threadID,0));
        }
        InitThreadVecTime(GetOne.threadID);
        ITforAllThread=AllThread.find(GetOne.threadID);
        if(AllThread.end()==ITforAllThread)
        {
            PIN_ReleaseLock(&lock);
            return;
        }
        map<UINT32, queue<map<UINT32, long> > >::iterator mapfindfather;
        mapfindfather=CreateThreadVecTime.find((UINT32)GetOne.object);
        if(mapfindfather!=CreateThreadVecTime.end())
        {
            ((ITforAllThread->second).ListAddress)->VecTime=(mapfindfather->second).front(); //从父线程中继承vec time
            (mapfindfather->second).pop();
            (((ITforAllThread->second).ListAddress)->VecTime).insert(pair<UINT32, long>(GetOne.threadID,1));
        }
        else
        {
            ITforAllThread=AllThread.find(GetOne.threadID);
            if(AllThread.end()==ITforAllThread)
            {
                PIN_ReleaseLock(&lock);
                return;
            }
            (((ITforAllThread->second).ListAddress)->VecTime).insert(pair<UINT32, long>(GetOne.threadID,1));
        }
    }
    
    PIN_ReleaseLock(&lock);
}

VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    PIN_GetLock(&lock, threadid+1);

    RecordType GetOne;
    GetOne=(RecordType) {2, threadid, 0};
    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }
    
    VectorDetect(GetOne.threadID,ITforAllThread->second);

    CreateNewFrame(threadid,ITforAllThread->second,"ThreadFini");

    FiniThreadVecTime[GetOne.threadID]=((ITforAllThread->second).ListAddress)->VecTime;
OutPutMetrics();
    PIN_ReleaseLock(&lock);
}

VOID BeforePthread_create(THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);

    RecordType GetOne;
    GetOne=(RecordType) {13, threadid, 0};
SynNum++;
    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }
    
    VectorDetect(GetOne.threadID,ITforAllThread->second);

    CreateNewFrame(threadid,ITforAllThread->second,"ThreadCreate");

    map<UINT32, queue<map<UINT32, long> > >::iterator mapforqueue; //to record the father thread's inf
    mapforqueue=CreateThreadVecTime.find(GetOne.threadID);
    if(mapforqueue==CreateThreadVecTime.end()) //the first time to create child thread
    {
        queue<map<UINT32, long> > queuetmp;
        queuetmp.push(((ITforAllThread->second).ListAddress)->VecTime);
        CreateThreadVecTime.insert(pair<UINT32, queue<map<UINT32, long> > >(GetOne.threadID, queuetmp));
    }
    else
    {
        (mapforqueue->second).push(((ITforAllThread->second).ListAddress)->VecTime);
    }

    PIN_ReleaseLock(&lock);
}

VOID AfterPthread_join(THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);
SynNum++;
    RecordType GetOne;
    map<THREADID, ThreadParent>::iterator itertmp;
    for(itertmp=ThreadMapParent.begin();itertmp!=ThreadMapParent.end();itertmp++)
    {
        if((itertmp->second).fatherthreadid==threadid)
            break;
    }
    if(itertmp!=ThreadMapParent.end())
    {
        GetOne=(RecordType) {3, threadid, (ADDRINT)(itertmp->first)};
        
        ThreadMapParent.erase(itertmp);
    }
    else
    {
        GetOne=(RecordType) {3, threadid, 0};
    }

    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }

    VectorDetect(GetOne.threadID,ITforAllThread->second);

    CreateNewFrame(threadid,ITforAllThread->second,"ThreadJoin");

    map<UINT32, long>::iterator mapvectimemain; //to find the thread's vec time
    map<UINT32, long>::iterator mapvectimechild; //to find child's vec time
    map<UINT32, ThreadVecTime>::iterator mapfini; //to find fini
    mapfini=AllThread.find((THREADID)(GetOne.object));
    if(mapfini==AllThread.end())
    {
        exit(-1);
    }
    for(mapvectimechild=(((mapfini->second).ListAddress)->VecTime).begin();mapvectimechild!=(((mapfini->second).ListAddress)->VecTime).end();mapvectimechild++)
    {
        mapvectimemain=(((ITforAllThread->second).ListAddress)->VecTime).find(mapvectimechild->first);
        if(mapvectimemain==(((ITforAllThread->second).ListAddress)->VecTime).end())
        {
            (((ITforAllThread->second).ListAddress)->VecTime)[mapvectimechild->first]=mapvectimechild->second+1;
        }
        else
        {
            mapvectimemain->second=(mapvectimechild->second+1) > mapvectimemain->second ? (mapvectimechild->second+1) : mapvectimemain->second;
        }
    }
    FiniThreadVecTime.erase((THREADID)(GetOne.object));

    PIN_ReleaseLock(&lock);
}

void JustBeforeLock(ADDRINT currentlock, THREADID threadid)
{
    RecordType GetOne;
    GetOne=(RecordType) {4, threadid, currentlock};
    LockNum++;
    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }

    VectorDetect(GetOne.threadID,ITforAllThread->second);
    CreateNewFrame(threadid,ITforAllThread->second,"MutexLock");
    
    map<ADDRINT, map<UINT32, long> >::iterator findthelock;
    findthelock=LockVecTime.find(currentlock);
    if(findthelock!=LockVecTime.end())
    {
        map<UINT32, long>::iterator mainvc, anothervc;
        for(anothervc=(findthelock->second).begin();anothervc!=(findthelock->second).end();anothervc++)
        {
            mainvc=(((ITforAllThread->second).ListAddress)->VecTime).find(anothervc->first);
            if(mainvc==(((ITforAllThread->second).ListAddress)->VecTime).end())
            {
                (((ITforAllThread->second).ListAddress)->VecTime)[anothervc->first]=anothervc->second;
            }
            else
            {
                mainvc->second=anothervc->second > mainvc->second ? anothervc->second : mainvc->second;
            }
        }
    }
}

VOID BeforePthread_mutex_lock(ADDRINT currentlock, THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);

    AfterLockInf[threadid]=currentlock;

    PIN_ReleaseLock(&lock);
}

VOID AfterPthread_mutex_lock(THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);

    map<THREADID, ADDRINT>::iterator findlock;
    findlock=AfterLockInf.find(threadid);
    if(findlock==AfterLockInf.end())
    {
        exit(-1);
    }

    JustBeforeLock(findlock->second, threadid);

    PIN_ReleaseLock(&lock);
}

VOID BeforePthread_mutex_trylock(ADDRINT currentlock, THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);

    TryLockInf[threadid]=currentlock;

    PIN_ReleaseLock(&lock);
}

VOID AfterPthread_mutex_trylock(int flag, THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);
    if(0==flag)
    {
        map<THREADID, ADDRINT>::iterator findlock;
        findlock=TryLockInf.find(threadid);
        if(findlock==TryLockInf.end())
        {
            exit(-1);
        }

        JustBeforeLock(findlock->second, threadid);
    }
    PIN_ReleaseLock(&lock);
}

/*please focus on the nesting cases*/
VOID BeforePthread_mutex_unlock(ADDRINT currentlock, THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);

    RecordType GetOne;
    GetOne=(RecordType) {5, threadid, currentlock};
    LockNum++;
    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }

    VectorDetect(GetOne.threadID,ITforAllThread->second);
    
    CreateNewFrame(threadid,ITforAllThread->second,"MutexUnlock");
    
    LockVecTime[currentlock]=((ITforAllThread->second).ListAddress)->VecTime;

    PIN_ReleaseLock(&lock);
}

VOID BeforePthread_cond_wait(ADDRINT cond, ADDRINT mutex, THREADID threadid)
{
	SynNum++;
    BeforePthread_mutex_unlock(mutex,threadid);
    PIN_GetLock(&lock, threadid+1);
    VecWait[threadid]=((RecordCond) {6, threadid, cond, mutex});
    PIN_ReleaseLock(&lock);
}

VOID AfterPthread_cond_wait(THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);

    RecordCond GetOne;
    map<THREADID, RecordCond>::iterator itwait;
    itwait=VecWait.find(threadid);
    if(itwait==VecWait.end())
    {
        exit(-1);
    }
    GetOne=(RecordCond) {(itwait->second).style, (itwait->second).threadID, (itwait->second).cond, (itwait->second).mutex};
    VecWait.erase(itwait);

    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        exit(-1);;
    }
    
    VectorDetect(GetOne.threadID,ITforAllThread->second);

    CreateNewFrame(threadid,ITforAllThread->second,"CondWait");

    map<UINT32, long>::iterator mapvectimemain;
    map<UINT32, long>::iterator mapvectimechild; //to find thread's vec time
    map<ADDRINT, map<UINT32, long> >::iterator mapsignal; //to find signal
    mapsignal=SignalVecTime.find(GetOne.cond);
    if(mapsignal==SignalVecTime.end())
    {
        PIN_ReleaseLock(&lock);
        return;
    }
    for(mapvectimechild=(mapsignal->second).begin();mapvectimechild!=(mapsignal->second).end();mapvectimechild++)
    {
        mapvectimemain=(((ITforAllThread->second).ListAddress)->VecTime).find(mapvectimechild->first);
        if(mapvectimemain==(((ITforAllThread->second).ListAddress)->VecTime).end())
        {
            (((ITforAllThread->second).ListAddress)->VecTime)[mapvectimechild->first]=mapvectimechild->second;
        }
        else
        {
            mapvectimemain->second=mapvectimechild->second > mapvectimemain->second ? mapvectimechild->second : mapvectimemain->second;
        }
    }

    JustBeforeLock(GetOne.mutex, threadid);

    PIN_ReleaseLock(&lock);
}

VOID BeforePthread_cond_timedwait(ADDRINT cond, ADDRINT mutex, THREADID threadid)
{
    BeforePthread_mutex_unlock(mutex,threadid);
	SynNum++;
    PIN_GetLock(&lock, threadid+1);
    VecWait[threadid]=((RecordCond) {7, threadid, cond, mutex});
    PIN_ReleaseLock(&lock);
}

VOID AfterPthread_cond_timedwait(THREADID threadid)
{
    AfterPthread_cond_wait(threadid);
}

VOID BeforePthread_cond_signal(ADDRINT cond, THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);
SynNum++;
    RecordType GetOne;
    GetOne=(RecordType) {8, threadid, cond};

    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }
    
    VectorDetect(GetOne.threadID,ITforAllThread->second);

    CreateNewFrame(threadid,ITforAllThread->second,"CondSignal");

    SignalVecTime.insert(pair<ADDRINT, map<UINT32, long> >(GetOne.object, ((ITforAllThread->second).ListAddress)->VecTime));

    PIN_ReleaseLock(&lock);
}

VOID BeforePthread_cond_broadcast(ADDRINT cond, THREADID threadid)
{
    PIN_GetLock(&lock, threadid+1);
SynNum++;
    RecordType GetOne;
    GetOne=(RecordType) {9, threadid, cond};

    map<UINT32, ThreadVecTime>::iterator ITforAllThread;
    ITforAllThread=AllThread.find(GetOne.threadID);
    if(AllThread.end()==ITforAllThread)
    {
        PIN_ReleaseLock(&lock);
        return;
    }
    
    VectorDetect(GetOne.threadID,ITforAllThread->second);

    CreateNewFrame(threadid,ITforAllThread->second,"CondBroadcast");

    SignalVecTime[GetOne.object]=((ITforAllThread->second).ListAddress)->VecTime;

    PIN_ReleaseLock(&lock);
}
