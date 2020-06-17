#include <unistd.h>
#include <string.h>
#include "structure.h"
#include "functions.h"
#include <pthread.h>
#include <math.h>

const int toiletNumber = 1;
const int potNumber = 1;
const int goodNumber = 3;
const int badNumber = 3;

#define ARRAY_COL 2
#define ARRAY_ROW 6

Person person;
Object ackObject;
int ackList[ARRAY_COL][ARRAY_ROW];
int rejectList[ARRAY_COL][ARRAY_ROW];
int listSize = 0;
Object sendObjects[ARRAY_COL];

pthread_mutex_t lamportMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t messageListMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t requestThread;
MPI_Datatype MPI_REQ;

MessageList *first = NULL;

Person init(int id, Object *toiletList, Object *potList)
{
    toiletList = malloc(sizeof(struct Object) * toiletNumber);
    potList = malloc(sizeof(struct Object) * potNumber);

    for (int i = 0; i < potNumber; i++)
    {
        Object pot;
        pot.id = i + 1;
        pot.noInList = i;
        pot.objectState = REPAIRED;
        pot.objectType = POT;
        potList[i] = pot;
    }

    for (int i = 0; i < toiletNumber; i++)
    {
        Object toilet;
        toilet.id = i + 1;
        toilet.noInList = i;
        toilet.objectState = REPAIRED;
        toilet.objectType = TOILET;
        toiletList[i] = toilet;
    }

    struct Person person;
    person.id = id;
    person.personType = person.id <= goodNumber ? GOOD : BAD;
    person.goodCount = goodNumber;
    person.badCount = badNumber;
    person.avaliableObjectsCount = person.personType - BAD ? 0 : toiletNumber + potNumber;
    person.toiletList = toiletList;
    person.potList = potList;
    person.priority = 0;
    person.messageCount = 0;
    person.lamportClock = 0;

    return person;
}

void waitRandomTime(int id)
{
    time_t tt;
    int quantum = time(&tt);
    srand(quantum + id);
    double seconds = ((double)(rand() % 1000)) / 500;
    // printf("Process: %d is waiting: %f\n", id, seconds);
    sleep(seconds);
}

void setupStructures()
{
    int nItems = 7;
    int blockLengths[7] = {1, 1, 1, 1, 1, 1, 1};
    MPI_Datatype types[7] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};

    MPI_Aint offsets[7];
    offsets[0] = offsetof(Request, id);
    offsets[1] = offsetof(Request, requestType);
    offsets[2] = offsetof(Request, objectId);
    offsets[3] = offsetof(Request, priority);
    offsets[4] = offsetof(Request, objectState);
    offsets[5] = offsetof(Request, objectType);
    offsets[6] = offsetof(Request, lamportClock);

    MPI_Type_create_struct(nItems, blockLengths, offsets, types, &MPI_REQ);
    MPI_Type_commit(&MPI_REQ);
}

int main(int argc, char **argv)
{
    int errs = 0;
    int provided, flag, claimed;

    MPI_Init_thread(0, 0, MPI_THREAD_MULTIPLE, &provided);

    int size, rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    setupStructures();

    MPI_Status status;
    if (rank == 0)
    {
        for (int i = 1; i <= (goodNumber + badNumber); i++)
        {
            printf("SYNCHR Message SENT to: %d\n", i);
            MPI_Send(&i, 1, MPI_INT, i, SYNCHR, MPI_COMM_WORLD);
        }
        int counter = 0;
        while (counter < (goodNumber + badNumber))
        {
            int sourceId;
            MPI_Recv(&sourceId, 1, MPI_INT, MPI_ANY_SOURCE, SYNCHR, MPI_COMM_WORLD, &status);
            printf("SYNCHR Message Received from: %d\n", sourceId);
            counter++;
        }
        printf("\nSYNCHR done!\n\n");
        for (int i = 1; i <= (goodNumber + badNumber); i++)
        {
            MPI_Send(&i, 1, MPI_INT, i, SYNCHR, MPI_COMM_WORLD);
        }
    }
    else
    {
        int id;
        MPI_Recv(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD, &status);
        struct Object *toiletList;
        struct Object *potList;
        person = init(rank, toiletList, potList);
        for (int i = 0; i < ARRAY_COL; i++)
        {
            sendObjects[i].id = -1;
            sendObjects[i].noInList = -1;
            sendObjects[i].objectState = -1;
            sendObjects[i].objectType = -1;
        }

        printf("Process: %d is Person: %d, %s\n", rank, person.id, person.personType - BAD ? "good" : "bad");
        MPI_Send(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD);
        MPI_Recv(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD, &status);
        waitRandomTime(id);

        pthread_create(&requestThread, NULL, handleRequests, 0);
        handleStates();

        pthread_join(requestThread, NULL);
    }
    free(person.toiletList);
    free(person.potList);
    MPI_Type_free(&MPI_REQ);
    MPI_Finalize();
    pthread_exit(NULL);
    return 0;
}

void handleStates()
{
    int state = INIT;
    int canGoCritical = -1, objectId = -1, objectType = -1, rejectedRest = false;
    while (true)
    {
        switch (state)
        {
        case INIT:
            for (int i = 0; i < (toiletNumber + potNumber); i++)
            {
                for (int j = 0; j < (goodNumber + badNumber); j++)
                {
                    rejectList[i][j] = 0;
                }
            }
            for (int i = 0; i < (toiletNumber + potNumber); i++)
                for (int j = 0; j < (goodNumber + badNumber); j++)
                    ackList[i][j] = 0;

            state = PREPARING;
            break;
        case PREPARING:
            pthread_mutex_lock(&messageListMutex);
            if (first != NULL)
            {
                preparingRequestHandler(first->currentRequest);
                MessageList *tmpFirst = first;
                first = first->nextMessage;
                free(tmpFirst);
            }
            pthread_mutex_unlock(&messageListMutex);
            int tempListASD = preparingState(rejectedRest);
            if (tempListASD > 0)
            {
                state = WAIT_CRITICAL;
            }
            break;
        case WAIT_CRITICAL:
            pthread_mutex_lock(&messageListMutex);
            if (first != NULL)
            {
                waitCriticalRequestHandler(first->currentRequest);
                MessageList *tmpFirst = first;
                first = first->nextMessage;
                free(tmpFirst);
            }
            pthread_mutex_unlock(&messageListMutex);
            canGoCritical = waitCriticalState(&objectId, &objectType);

            if (canGoCritical == true)
            {
                if (objectType == TOILET && objectId > 0)
                {
                    ackObject = person.toiletList[objectId - 1];
                }
                else if (objectType == POT && objectId > 0)
                {
                    ackObject = person.potList[objectId - 1];
                }
                rejectedRest = false;
                state = IN_CRITICAL;
            }
            else if (canGoCritical == false)
            {
                rejectedRest = true;
                state = REST;
            }
            break;
        case IN_CRITICAL:
            inCriticalState();
            state = AFTER_CRITICAL;
            break;
        case AFTER_CRITICAL:
            afterCriticalState(&ackObject);
            state = REST;
            break;
        case REST:
            pthread_mutex_lock(&messageListMutex);
            if (first != NULL)
            {
                restRequestHandler(first->currentRequest, objectId, objectType);
                MessageList *tmpFirst = first;
                first = first->nextMessage;
                free(tmpFirst);
            }
            pthread_mutex_unlock(&messageListMutex);
            pthread_mutex_lock(&messageListMutex);
            if (first == NULL)
            {
                state = PREPARING;
            }
            pthread_mutex_unlock(&messageListMutex);
            break;
        default:
            break;
        }
    }
    free(ackList);
    free(rejectList);
}

void *handleRequests()
{
    while (true)
    {
        MPI_Status status;
        Request request;
        MPI_Recv(&request, 1, MPI_REQ, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_ERROR == MPI_SUCCESS)
        {

            pthread_mutex_lock(&messageListMutex);
            if (first == NULL)
            {
                first = malloc(sizeof(MessageList));
                first->currentRequest = request;
                first->nextMessage = NULL;
            }
            else
            {
                MessageList *tempMessage = first;
                while (tempMessage->nextMessage != NULL)
                {
                    tempMessage = tempMessage->nextMessage;
                }
                MessageList *currentMessage = malloc(sizeof(MessageList));
                currentMessage->currentRequest = request;
                currentMessage->nextMessage = NULL;
                tempMessage->nextMessage = currentMessage;
            }
            pthread_mutex_unlock(&messageListMutex);
            pthread_mutex_lock(&lamportMutex);
            person.lamportClock = request.lamportClock > person.lamportClock ? request.lamportClock + 1 : person.lamportClock + 1;
            pthread_mutex_unlock(&lamportMutex);
        }
    }
}

void preparingRequestHandler(Request request)
{
    Request deepRequest;
    deepRequest.id = person.id;
    deepRequest.objectId = request.objectId;
    deepRequest.objectState = request.objectState;
    deepRequest.objectType = request.objectType;
    deepRequest.priority = request.priority;
    deepRequest.lamportClock = person.lamportClock;
    deepRequest.requestType = request.requestType;
    int receivedId = request.id;
    if (deepRequest.requestType == PREQ)
    {
        deepRequest.requestType = PACK;
        updateLamportClock();
        // printf("[%d]\tPREPARING, %d: Send PACK to: %d about %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
    }
    else if (deepRequest.requestType == TREQ)
    {
        deepRequest.requestType = TACK;
        updateLamportClock();
        //printf("[%d]\tPREPARING, %d: Send TACK to: %d about %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
    }
    else if (deepRequest.requestType == ACKALL)
    {
        updateLists(request, "PREPARING");
        // for (int i = 0; i < listSize; i++)
        // {
        //     if (sendObjects[i].objectType == request.objectType && sendObjects[i].id == request.objectId)
        //     {
        //         sendObjects[i].objectState = request.objectState;
        //     }
        // }
        for (int i = 0; i < listSize; i++)
        {
            if (request.objectType == sendObjects[i].objectType)
            {
                if (request.objectId != sendObjects[i].id)
                {
                    // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                    // {
                        if(ackList[i][request.id - 1] == 1){
                            ackList[i][request.id - 1] = 0;
                            rejectList[i][request.id - 1] = 1;
                        }else{
                            ackList[i][request.id - 1] = 1;
                        }
                    //     rejectList[i][request.id - 1] = 0;
                    // }
                    // else
                    // {
                    //     ackList[i][request.id - 1] = 0;
                    //     rejectList[i][request.id - 1] = 1;
                    // }
                }
                else
                {
                    rejectList[i][request.id - 1] = 1;
                    // ackList[i][request.id - 1] = 0;
                }
            }
        }
    }
    else if (deepRequest.requestType == PACK)
    {
        //printf("[%d]\tPREPARING, %d: Receive PACK from: %d\n", person.lamportClock, person.id, receivedId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == POT && sendObjects[i].id == request.objectId)
            {
                // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                // {
                    ackList[i][request.id - 1] = 1;
                    // rejectList[i][request.id - 1] = 0;
                // }
                // else
                // {
                //     ackList[i][request.id - 1] = 0;
                //     rejectList[i][request.id - 1] = 1;
                // }
            }
        }
    }
    else if (deepRequest.requestType == TACK)
    {
        //printf("[%d]\tPREPARING, %d: Receive TACK from: %d about: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == TOILET && sendObjects[i].id == request.objectId)
            {
                // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                // {
                    ackList[i][request.id - 1] = 1;
                    // rejectList[i][request.id - 1] = 0;
                // }
                // else
                // {
                //     ackList[i][request.id - 1] = 0;
                //     rejectList[i][request.id - 1] = 1;
                // }
            }
        }
    }
    else if (deepRequest.requestType == REJECT)
    {
        // printf("[%d]\tPREPARING, %d: Receive REJECT from: %d\n", person.lamportClock, person.id, receivedId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].id == request.objectId && sendObjects[i].objectType == request.objectType)
            {
                rejectList[i][request.id - 1] = 1;
                // ackList[i][request.id - 1] = 0;
            }
        }
    }
    else
    {
        //default
    }
}

void waitCriticalRequestHandler(Request request)
{
    int tempListSize;
    Request deepRequest;
    deepRequest.id = person.id;
    deepRequest.objectId = request.objectId;
    deepRequest.objectState = request.objectState;
    deepRequest.objectType = request.objectType;
    deepRequest.priority = request.priority;
    deepRequest.lamportClock = person.lamportClock;
    deepRequest.requestType = request.requestType;
    int receivedId = request.id;
    int isPreviousRequest = abs(request.priority - person.priority) >= 5;
    if (deepRequest.requestType == PREQ)
    {

        //printf("[%d]\tWAIT_CRITICAL, %d: Receive PREQ from id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        if ((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount))
        {
            deepRequest.requestType = PACK;
            updateLamportClock();
            //printf("[%d]\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
        }
        else
        {
            int arePotsInList = false;
            int tempListSize = listSize;
            for (int i = 0; i < tempListSize; i++)
            {
                if (sendObjects[i].objectType == POT)
                {
                    arePotsInList = true;
                    break;
                }
            }

            if (arePotsInList == false)
            {
                deepRequest.requestType = PACK;
                updateLamportClock();
                //printf("[%d]\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
            }
            else
            {
                int arePotsWithIdInList = false;
                int tempListSize = listSize;
                for (int i = 0; i < tempListSize; i++)
                {
                    if (sendObjects[i].objectType == POT && sendObjects[i].id == deepRequest.objectId)
                    {
                        arePotsWithIdInList = true;

                        if (person.priority > deepRequest.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                        {
                            deepRequest.requestType = PACK;
                            updateLamportClock();
                            //printf("[%d]\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                        }
                        else if (person.priority < deepRequest.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                        {
                            updateLamportClock();
                            deepRequest.requestType = REJECT;
                            // printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                        }
                        else //równe priorytety
                        {
                            if (person.id > receivedId)
                            {
                                deepRequest.requestType = REJECT;
                                updateLamportClock();
                                // printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                                MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                            }
                            else
                            {
                                deepRequest.requestType = PACK;
                                updateLamportClock();
                                //printf("[%d]\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                                MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
                if (arePotsWithIdInList == false)
                {
                    deepRequest.requestType = PACK;
                    updateLamportClock();
                    //printf("[%d]\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                    MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                }
            }
        }
        // //printf("Lamport: %d, ReceivedId: %d, PersonId: %d, objectType: %d, ObjectState: %d, personType: %d, Person priorytet: %d, request priority: %d\n", person.lamportClock, receivedId, person.id, request.objectType, request.objectState, person.personType, person.priority, request.priority);
    }
    else if (deepRequest.requestType == TREQ)
    {

        //printf("[%d]\tWAIT_CRITICAL, %d: Receive TREQ from id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        if ((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount))
        {
            deepRequest.requestType = TACK;
            updateLamportClock();
            //printf("[%d]\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
        }
        else
        {
            int areToiletsInList = false;
            int tempListSize = listSize;
            for (int i = 0; i < tempListSize; i++)
            {
                if (sendObjects[i].objectType == TOILET)
                {
                    areToiletsInList = true;
                    break;
                }
            }

            if (areToiletsInList == false)
            {
                deepRequest.requestType = TACK;
                updateLamportClock();
                //printf("[%d]\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
            }
            else
            {
                int areToiletsWithIdInList = false;
                for (int i = 0; i < listSize; i++)
                {
                    if (sendObjects[i].objectType == TOILET && sendObjects[i].id == request.objectId)
                    {
                        areToiletsWithIdInList = true;

                        if (person.priority > deepRequest.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                        {
                            deepRequest.requestType = TACK;
                            updateLamportClock();
                            //printf("[%d]\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                        }
                        else if (person.priority < deepRequest.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                        {
                            deepRequest.requestType = REJECT;
                            updateLamportClock();
                            // printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                        }
                        else //równe priorytety
                        {
                            if (person.id > receivedId)
                            {
                                deepRequest.requestType = REJECT;
                                updateLamportClock();
                                // printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                                MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                            }
                            else
                            {
                                deepRequest.requestType = TACK;
                                updateLamportClock();
                                //printf("[%d]\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                                MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
                if (areToiletsWithIdInList == false)
                {
                    deepRequest.requestType = TACK;
                    updateLamportClock();
                    //printf("[%d]\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                    MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                }
            }
        }
    }
    else if (deepRequest.requestType == ACKALL)
    {
        updateLists(request, "WAIT_CRITICAL");

        // for (int i = 0; i < listSize; i++)
        // {
        //     if (sendObjects[i].objectType == request.objectType && sendObjects[i].id == request.objectId)
        //     {
        //         sendObjects[i].objectState = request.objectState;
        //     }
        // }
        for (int i = 0; i < listSize; i++)
        {
            if (request.objectType == sendObjects[i].objectType)
            {
                if (request.objectId != sendObjects[i].id)
                {
                    // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                    // {
                        if(ackList[i][request.id - 1] == 1){
                            ackList[i][request.id - 1] = 0;
                            rejectList[i][request.id - 1] = 1;
                        }else{
                            ackList[i][request.id - 1] = 1;
                        }
                        
                        // rejectList[i][request.id - 1] = 0;
                    // }
                    // else
                    // {
                    //     ackList[i][request.id - 1] = 0;
                    //     rejectList[i][request.id - 1] = 1;
                    // }
                }
                else
                {
                    rejectList[i][request.id - 1] = 1;
                    ackList[i][request.id - 1] = 0;
                }
            }
        }
    }
    else if (deepRequest.requestType == PACK)
    {
        // if (!isPreviousRequest)
        // {
        //printf("[%d]\tWAIT_CRITICAL, %d: Receive PACK from: %d\n", person.lamportClock, person.id, receivedId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == POT && sendObjects[i].id == request.objectId)
            {
                // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                // {
                    ackList[i][request.id - 1] = 1;
                    // rejectList[i][request.id - 1] = 0;
                // }
                // else
                // {
                //     // ackList[i][request.id - 1] = 0;
                //     rejectList[i][request.id - 1] = 1;
                // }
            }
        }
        // }
    }
    else if (deepRequest.requestType == TACK)
    {
        // if (!isPreviousRequest)
        // {
        //printf("[%d]\tWAIT_CRITICAL, %d: Receive TACK from: %d about: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == TOILET && sendObjects[i].id == request.objectId)
            {
                // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                // {
                    ackList[i][request.id - 1] = 1;
                    // rejectList[i][request.id - 1] = 0;
                // }
                // else
                // {
                    // ackList[i][request.id - 1] = 0;
                    rejectList[i][request.id - 1] = 1;
                // }
            }
        }
        // }
    }
    else if (deepRequest.requestType == REJECT)
    {
        // if (!isPreviousRequest)
        // {
        //printf("[%d]\tWAIT_CRITICAL, %d: Receive REJECT from: %d\n", person.lamportClock, person.id, receivedId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].id == request.objectId && sendObjects[i].objectType == request.objectType)
            {
                rejectList[i][request.id - 1] = 1;
                // ackList[i][request.id - 1] = 0;
            }
        }
        // }
    }
    else
    {
        //printf("[%d]\tWAIT_CRITICAL, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
    }
}

void restRequestHandler(Request request, int objectId, int objectType)
{
    Request deepRequest;
    deepRequest.id = person.id;
    deepRequest.objectId = request.objectId;
    deepRequest.objectState = request.objectState;
    deepRequest.objectType = request.objectType;
    deepRequest.priority = request.priority;
    deepRequest.lamportClock = person.lamportClock;
    deepRequest.requestType = request.requestType;
    int receivedId = request.id;
    if (deepRequest.requestType == PREQ)
    {
        updateLamportClock();
        // if (deepRequest.objectType == objectType && deepRequest.objectId == objectId && !((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount)))
        // {
        //     deepRequest.requestType = REJECT;
        //     MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
        // }
        // else
        // {
        deepRequest.requestType = PACK;
        MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
        // }
        //printf("[%d]\tREST, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
    }
    else if (deepRequest.requestType == TREQ)
    {
        updateLamportClock();
        // if (deepRequest.objectType == objectType && deepRequest.objectId == objectId && !((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount)))
        // {
        //     deepRequest.requestType = REJECT;
        //     MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
        // }
        // else
        // {
        deepRequest.requestType = TACK;
        //printf("[%d]\tREST, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
        // }
    }
    else if (deepRequest.requestType == ACKALL)
    {
        updateLists(request, "REST");

        // for (int i = 0; i < listSize; i++)
        // {
        //     if (sendObjects[i].objectType == request.objectType && sendObjects[i].id == request.objectId)
        //     {
        //         sendObjects[i].objectState = request.objectState;
        //     }
        // }
        for (int i = 0; i < listSize; i++)
        {
            if (request.objectType == sendObjects[i].objectType)
            {
                if (request.objectId != sendObjects[i].id)
                {
                    // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                    // {
                        if(ackList[i][request.id - 1] == 1){
                            ackList[i][request.id - 1] = 0;
                            rejectList[i][request.id - 1] = 1;
                        }else{
                            ackList[i][request.id - 1] = 1;
                        }
                    //     rejectList[i][request.id - 1] = 0;
                    // }
                    // else
                    // {
                    //     ackList[i][request.id - 1] = 0;
                    //     rejectList[i][request.id - 1] = 1;
                    // }
                }
                else
                {
                    rejectList[i][request.id - 1] = 1;
                    // ackList[i][request.id - 1] = 0;
                }
            }
        }
    }
    else if (deepRequest.requestType == PACK)
    {
        // if (!isPreviousRequest)
        // {
        //printf("[%d]\tREST, %d: Receive PACK from: %d\n", person.lamportClock, person.id, receivedId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == POT && sendObjects[i].id == request.objectId)
            {
                // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                // {
                    ackList[i][request.id - 1] = 1;
                    // rejectList[i][request.id - 1] = 0;
                // }
                // else
                // {
                //     ackList[i][request.id - 1] = 0;
                //     rejectList[i][request.id - 1] = 1;
                // }
            }
        }
        // }
    }
    else if (deepRequest.requestType == TACK)
    {
        // if (!isPreviousRequest)
        // {
        //printf("[%d]\tREST, %d: Receive TACK from: %d about: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == TOILET && sendObjects[i].id == request.objectId)
            {
                // if ((person.personType == GOOD && request.objectState == BROKEN && sendObjects[i].objectState == BROKEN) || (person.personType == BAD && request.objectState == REPAIRED && sendObjects[i].objectState == REPAIRED))
                // {
                    ackList[i][request.id - 1] = 1;
                //     rejectList[i][request.id - 1] = 0;
                // }
                // else
                // {
                //     ackList[i][request.id - 1] = 0;
                //     rejectList[i][request.id - 1] = 1;
                // }
            }
        }
        // }
    }
    else if (deepRequest.requestType == REJECT)
    {
        // if (!isPreviousRequest)
        // {
        //printf("[%d]\tREST, %d: Receive REJECT from: %d\n", person.lamportClock, person.id, receivedId);
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].id == request.objectId && sendObjects[i].objectType == request.objectType)
            {
                rejectList[i][request.id - 1] = 1;
            }
        }
        // }
    }
    else
    {
        //printf("[%d]\tREST, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
    }
}

void inCriticalState()
{
    printf("[%d]\tIN_CRITICAL, %d: process is in cricital section\n", person.lamportClock, person.id);
    waitRandomTime(person.id);
}

void afterCriticalState(Object *object)
{
    Request request;
    request.id = person.id;
    request.requestType = ACKALL;
    request.objectId = object->id;
    for(int i = 0; i < ARRAY_COL; i++){
        if(object->objectType== TOILET){
            if(object->id == person.toiletList[i].id){
                request.objectState = person.toiletList[i].objectState== BROKEN ? REPAIRED : BROKEN;
            }
        }else{
                if(object->id == person.potList[i].id){
                request.objectState = person.potList[i].objectState== BROKEN ? REPAIRED : BROKEN;
            }
        }
    }
    request.objectType = object->objectType;
    request.lamportClock = person.lamportClock;
    request.priority = person.lamportClock;
    updateLists(request, "AFTER_CRITICAL");
    for (int i = 1; i <= (person.goodCount + person.badCount); i++)
    {
        if (i != person.id)
        {
            updateLamportClock();
            // printf("[%d]\tAFTER_CRITICAL, %d: SEND ACKALL to id: %d about objectId: %d\n", person.lamportClock, person.id, i, request.objectId);
            MPI_Send(&request, 1, MPI_REQ, i, ACKALL, MPI_COMM_WORLD);
        }
    }

    sendObjects[object->id - 1].id = -1;
    sendObjects[object->id - 1].noInList = -1;
    sendObjects[object->id - 1].objectState = -1;
    sendObjects[object->id - 1].objectType = -1;
}

int preparingState(int rejectedRest)
{
    int tempListSize = listSize;
    for (int i = 0; i < listSize; i++)
    {
        int columnACK = 0;
        int columnRJCK = 0;
        if (sendObjects[i].id != -1)
        {
            for (int j = 0; j < (goodNumber + badNumber); j++)
            {
                columnACK += ackList[i][j];
                columnRJCK += rejectList[i][j];
            }
            if ((columnACK + columnRJCK) >= (goodNumber + badNumber - 1))
            {
                for (int j = i; j < listSize - 1; j++)
                {
                    sendObjects[j] = sendObjects[j + 1];
                    sendObjects[j + 1].id = -1;
                    sendObjects[j + 1].noInList = -1;
                    sendObjects[j + 1].objectState = -1;
                    sendObjects[j + 1].objectType = -1;
                    for (int k = 0; k < ARRAY_ROW; k++)
                    {
                        rejectList[j][k] = rejectList[j + 1][k];
                        ackList[j][k] = ackList[j + 1][k];
                    }
                }
                listSize--;
            }
        }
    }
    if ((potNumber + toiletNumber - listSize) > 0)
    {
        int iter = 0;
        Object *objectList = malloc(sizeof(Object) * (potNumber + toiletNumber - listSize));
        if (person.personType - BAD)
        {
            // good
            for (int i = 0; i < toiletNumber; i++)
            {
                if (person.toiletList[i].objectState == BROKEN)
                {
                    int isObjectNotInList = true;
                    for (int j = 0; j < listSize; j++)
                    {

                        if (person.toiletList[i].id == sendObjects[j].id && sendObjects[j].objectType == TOILET)
                        {
                            isObjectNotInList = false;
                        }
                    }
                    if (isObjectNotInList)
                    {
                        objectList[iter] = person.toiletList[i];
                        iter++;
                    }
                }
            }
            for (int i = 0; i < potNumber; i++)
            {
                if (person.potList[i].objectState == BROKEN)
                {
                    int isObjectNotInList = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (person.potList[i].id == sendObjects[j].id && sendObjects[j].objectType == POT)
                        {
                            isObjectNotInList = false;
                        }
                    }
                    if (isObjectNotInList)
                    {
                        objectList[iter] = person.potList[i];
                        iter++;
                    }
                }
            }
        }
        else
        {
            // bad
            for (int i = 0; i < toiletNumber; i++)
            {
                if (person.toiletList[i].objectState == REPAIRED)
                {
                    int isObjectNotInList = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (person.toiletList[i].id == sendObjects[j].id && sendObjects[j].objectType == TOILET)
                        {
                            isObjectNotInList = false;
                        }
                    }
                    if (isObjectNotInList)
                    {
                        objectList[iter] = person.toiletList[i];
                        iter++;
                    }
                }
            }
            for (int i = 0; i < potNumber; i++)
            {
                if (person.potList[i].objectState == REPAIRED)
                {
                    int isObjectNotInList = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (person.potList[i].id == sendObjects[j].id && sendObjects[j].objectType == POT)
                        {
                            isObjectNotInList = false;
                        }
                    }
                    if (isObjectNotInList)
                    {
                        objectList[iter] = person.potList[i];
                        iter++;
                    }
                }
            }
        }

        sendRequestForObjects(objectList, iter, rejectedRest);
        int result = listSize + iter;
        for (int i = listSize, j = 0; j < iter; i++, j++)
        {
            sendObjects[i] = objectList[j];
        }
        for (int i = listSize; i < ARRAY_COL; i++)
        {
            for (int j = 0; j < ARRAY_ROW; j++)
            {
                ackList[i][j] = 0;
                rejectList[i][j] = 0;
            }
        }
        free(objectList);
        listSize = result;
        return result;
    }
    else
        return -1;
}

void sendRequestForObjects(Object *ObjectList, int iterator, int rejectedRest)
{
    Request req;
    person.priority = rejectedRest ? person.priority + 5 : person.lamportClock;

    for (int i = 0; i < iterator; i++)
    {
        req.id = person.id;
        req.objectId = ObjectList[i].id;
        req.requestType = ObjectList[i].objectType == TOILET ? TREQ : PREQ;
        req.objectType = ObjectList[i].objectType == TOILET ? TOILET : POT;
        req.objectState = ObjectList[i].objectState;
        for (int j = 1; j <= (person.goodCount + person.badCount); j++)
        {
            if (j != person.id)
            {
                time_t tt;
                int quantum = time(&tt);
                srand(quantum + person.id);
                int priority = rand() % 5;
                req.lamportClock = person.lamportClock;
                updateLamportClock();
                req.priority = person.priority + priority;
                // printf("[%d]\tPREPARING, %d: Send %s to: %d about %d\n", person.lamportClock, person.id, req.requestType == TREQ ? "TREQ" : "PREQ", j, req.objectId);
                MPI_Send(&req, 1, MPI_REQ, j, req.requestType, MPI_COMM_WORLD);
            }
        }
    }
}

void updateLists(Request request, char *stateName)
{
    int receivedId = request.id;
    if (request.objectType == POT)
    {
        //printf("[%d]\t%s, %d: Receive ACK_ALL with pot: %d and state: %s\n", person.lamportClock, stateName, person.id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
        person.potList[request.objectId - 1].objectState = request.objectState;
        if (person.personType == GOOD)
        {
            person.avaliableObjectsCount = toiletNumber + potNumber - listSize; // ????
        }
        else
        {
            person.avaliableObjectsCount = toiletNumber + potNumber - listSize;
        }
    }
    else
    {
        //printf("[%d]\t%s, %d: Receive ACK_ALL with toilet: %d and state: %s\n", person.lamportClock, stateName, person.id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
        person.toiletList[request.objectId - 1].objectState = request.objectState;
        if (person.personType == GOOD)
        {
            person.avaliableObjectsCount = toiletNumber + potNumber - listSize;
        }
        else
        {
            person.avaliableObjectsCount = toiletNumber + potNumber - listSize;
        }
    }
}

void updateLamportClock()
{
    pthread_mutex_lock(&lamportMutex);
    person.lamportClock += 1;
    pthread_mutex_unlock(&lamportMutex);
}

int waitCriticalState(int *objectId, int *objectType)
{
    int areAllRejected = true;

    for (int i = 0; i < listSize; i++)
    {
        int rejectCounter = 0;
        for (int j = 0; j < (goodNumber + badNumber); j++)
        {
            rejectCounter += rejectList[i][j];
        }
        if (rejectCounter == 0)
        {
            areAllRejected = false;
            break;
        }
    }

    
    for (int i = 0; i < listSize; i++)
    {
        int ackCounter = 0;
        for (int j = 0; j < (goodNumber + badNumber); j++)
        {
            ackCounter += ackList[i][j];
        }

        if (ackCounter == (person.goodCount + person.badCount - 1))
        {
            if (sendObjects[i].objectType == TOILET)
            {
                for(int j = 0; j < ARRAY_COL; j++){
                    if(sendObjects[i].objectType == person.toiletList[j].objectType && sendObjects[i].id == person.toiletList[j].id){

                        if (sendObjects[i].objectState == person.toiletList[j].objectState)
                        {
                            printf("[%d]\tWAIT_CRITICAL, %d: ACK for %s %d is given, going to IN_CRITICAL, there are %d avaliableObjects\n", person.lamportClock, person.id, sendObjects[i].objectType == TOILET ? "TOILET" : "POT", sendObjects[i].id, person.avaliableObjectsCount);
                            *objectId = sendObjects[i].id;
                            *objectType = sendObjects[i].objectType;
                            return true;
                        }
                    }
                }
            }
            else
            {
                for(int j = 0; j < ARRAY_COL; j++){
                    if(sendObjects[i].objectType == person.potList[j].objectType && sendObjects[i].id == person.potList[j].id){

                        if (sendObjects[i].objectState == person.potList[j].objectState)
                        {
                            printf("[%d]\tWAIT_CRITICAL, %d: ACK for %s %d is given, going to IN_CRITICAL, there are %d avaliableObjects\n", person.lamportClock, person.id, sendObjects[i].objectType == TOILET ? "TOILET" : "POT", sendObjects[i].id, person.avaliableObjectsCount);
                            *objectId = sendObjects[i].id;
                            *objectType = sendObjects[i].objectType;
                            return true;
                        }
                    }
                }
            }
        }
    }
    if (areAllRejected)
    {
        //printf("[%d]\tWAIT_CRITICAL, %d: List is empty, going to rest\n", person.lamportClock, person.id);
        return false;
    }
    return -1;
}