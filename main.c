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

#define ARRAY_SIZE 2

Person person;
Object ackObject;
int ackList[ARRAY_SIZE];
int rejectList[ARRAY_SIZE];
int listSize;
Object sendObjects[ARRAY_SIZE];
int objectTypeIsChanged[ARRAY_SIZE];
int iterationsCounter;
int iterator;

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
    //printf("Process: %d is waiting: %f\n", id, seconds);
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
            //printf("SYNCHR Message SENT to: %d\n", i);
            MPI_Send(&i, 1, MPI_INT, i, SYNCHR, MPI_COMM_WORLD);
        }
        int counter = 0;
        while (counter < (goodNumber + badNumber))
        {
            int sourceId;
            MPI_Recv(&sourceId, 1, MPI_INT, MPI_ANY_SOURCE, SYNCHR, MPI_COMM_WORLD, &status);
            //printf("SYNCHR Message Received from: %d\n", sourceId);
            counter++;
        }
        //printf("\nSYNCHR done!\n\n");
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
        for (int i = 0; i < ARRAY_SIZE; i++)
        {
            sendObjects[i].id = -1;
            sendObjects[i].noInList = -1;
            sendObjects[i].objectState = -1;
            sendObjects[i].objectType = -1;
        }

        //printf("Process: %d is Person: %d, %s\n", rank, person.id, person.personType - BAD ? "good" : "bad");
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
            iterationsCounter = 0;
            for (int i = 0; i < ARRAY_SIZE; i++)
            {
                ackList[i] = 0;
                rejectList[i] = 0;
                objectTypeIsChanged[i] = false;
            }
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
            iterator = preparingState(rejectedRest);
            if (iterator > 0)
            {
                state = WAIT_CRITICAL;
            }
            break;
        case WAIT_CRITICAL:
            pthread_mutex_lock(&messageListMutex);
            if (first != NULL)
            {
                waitCriticalRequestHandler(first->currentRequest, sendObjects);
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
                restRequestHandler(first->currentRequest);
                MessageList *tmpFirst = first;
                first = first->nextMessage;
                free(tmpFirst);
            }
            pthread_mutex_unlock(&messageListMutex);
            state = PREPARING;
            break;
        default:
            //printf("default");
            break;
        }
    }
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
    deepRequest.requestType = request.requestType;
    int receivedId = request.id;
    if (deepRequest.requestType == PREQ)
    {
        deepRequest.requestType = PACK;
        updateLamportClock();
        //printf("[%d]\tPREPARING, %d: Send PACK to: %d about %d\n", person.lamportClock, person.id, receivedId, request.objectId);
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
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == POT)
            {
                if (sendObjects[i].objectState != person.potList[sendObjects[i].id - 1].objectState)
                {
                    objectTypeIsChanged[i] = true;
                }
            }
            else
            {
                if (sendObjects[i].objectState != person.toiletList[sendObjects[i].id - 1].objectState)
                {
                    objectTypeIsChanged[i] = true;
                }
            }
        }
    }
    else if (deepRequest.requestType == PACK)
    {
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                ackList[i]++;
            }
        }
        //printf("[%d]\tPREPARING, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
    }
    else if (deepRequest.requestType == TACK)
    {
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                ackList[i]++;
            }
        }
        //printf("[%d]\tPREPARING, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
    }
    else if (deepRequest.requestType == REJECT)
    {
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                rejectList[i]++;
            }
        }
        //printf("[%d]\tPREPARING, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
    }
    else
    {
        //default
    }
}

void waitCriticalRequestHandler(Request request, Object *objectList)
{
    int tempListSize;
    Request deepRequest;
    deepRequest.id = person.id;
    deepRequest.objectId = request.objectId;
    deepRequest.objectState = request.objectState;
    deepRequest.objectType = request.objectType;
    deepRequest.priority = request.priority;
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
                if (objectList[i].objectType == POT)
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
                    if (objectList[i].objectType == POT && objectList[i].id == deepRequest.objectId)
                    {
                        arePotsWithIdInList = true;

                        if (person.priority > deepRequest.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                        {
                            deepRequest.requestType = PACK;
                            updateLamportClock();
                            //printf("[%d]\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                        }
                        else if (person.priority < request.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                        {
                            deepRequest.requestType = REJECT;
                            updateLamportClock();
                            //printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                        }
                        else //równe priorytety
                        {
                            if (person.id > receivedId)
                            {
                                deepRequest.requestType = REJECT;
                                updateLamportClock();
                                //printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
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
                if (objectList[i].objectType == TOILET)
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
                    if (objectList[i].objectType == TOILET && objectList[i].id == request.objectId)
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
                            //printf("[%d]\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
                            MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                        }
                        else //równe priorytety
                        {
                            if (person.id > receivedId)
                            {
                                deepRequest.requestType = REJECT;
                                updateLamportClock();
                                //printf("[%d]\t addsd asdasd WAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
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
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == POT)
            {
                if (sendObjects[i].objectState != person.potList[sendObjects[i].id - 1].objectState)
                {
                    objectTypeIsChanged[i] = true;
                }
            }
            else
            {
                if (sendObjects[i].objectState != person.toiletList[sendObjects[i].id - 1].objectState)
                {
                    objectTypeIsChanged[i] = true;
                }
            }
        }
    }
    else if (deepRequest.requestType == PACK)
    {
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                ackList[i]++;
            }
        }
    }
    else if (deepRequest.requestType == TACK)
    {

        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                ackList[i]++;
            }
        }
    }
    else if (deepRequest.requestType == REJECT)
    {
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                rejectList[i]++;
            }
        }
    }
    else
    {
        //printf("[%d]\tWAIT_CRITICAL, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
    }
}

void restRequestHandler(Request request)
{
    Request deepRequest;
    deepRequest.id = person.id;
    deepRequest.objectId = request.objectId;
    deepRequest.objectState = request.objectState;
    deepRequest.objectType = request.objectType;
    deepRequest.priority = request.priority;
    deepRequest.requestType = request.requestType;
    int receivedId = request.id;
    if (deepRequest.requestType == PREQ)
    {
        deepRequest.requestType = PACK;
        updateLamportClock();
        //printf("[%d]\tREST, %d: SEND PACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
    }
    else if (deepRequest.requestType == TREQ)
    {
        deepRequest.requestType = TACK;
        updateLamportClock();
        //printf("[%d]\tREST, %d: SEND TACK to id: %d and objectId: %d\n", person.lamportClock, person.id, receivedId, request.objectId);
        MPI_Send(&deepRequest, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
    }
    else if (deepRequest.requestType == ACKALL)
    {
        updateLists(request, "WAIT_CRITICAL");
        for (int i = 0; i < listSize; i++)
        {
            if (sendObjects[i].objectType == POT)
            {
                if (sendObjects[i].objectState != person.potList[sendObjects[i].id - 1].objectState)
                {
                    objectTypeIsChanged[i] = true;
                }
            }
            else
            {
                if (sendObjects[i].objectState != person.toiletList[sendObjects[i].id - 1].objectState)
                {
                    objectTypeIsChanged[i] = true;
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
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                ackList[i]++;
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
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                ackList[i]++;
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
            if (sendObjects[i].objectType == request.objectType && request.objectId == sendObjects[i].id)
            {
                rejectList[i]++;
            }
        }
        // }
    }
    else
    {
        //printf("[%d]\tWAIT_CRITICAL, %d: Received ignore message. %d\n", person.lamportClock, person.id, deepRequest.requestType);
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
    if (object->objectType == TOILET)
    {
        request.objectState = person.toiletList[object->id - 1].objectState == BROKEN ? REPAIRED : BROKEN;
    }
    else
    {
        request.objectState = person.potList[object->id - 1].objectState == BROKEN ? REPAIRED : BROKEN;
    }
    request.priority = person.lamportClock;
    updateLists(request, "AFTER_CRITICAL");
    for (int i = 1; i <= (person.goodCount + person.badCount); i++)
    {
        if (i != person.id)
        {
            updateLamportClock();
            printf("[%d]\tAFTER_CRITICAL, %d: SEND ACKALL to id: %d about objectId: %d\n", person.lamportClock, person.id, i, request.objectId);
            MPI_Send(&request, 1, MPI_REQ, i, ACKALL, MPI_COMM_WORLD);
        }
    }
}

int preparingState(int rejectedRest)
{
    for (int i = 0; i < listSize; i++)
    {
        if ((rejectList[i] + ackList[i]) >= (goodNumber + badNumber - 1))
        {

            for (int j = i; j < listSize - 1; j++)
            {
                sendObjects[j].id = sendObjects[j + 1].id;
                sendObjects[j].noInList = sendObjects[j + 1].noInList;
                sendObjects[j].objectState = sendObjects[j + 1].objectState;
                sendObjects[j].objectType = sendObjects[j + 1].objectType;
                sendObjects[j + 1].id = -1;
                sendObjects[j + 1].noInList = -1;
                sendObjects[j + 1].objectState = -1;
                sendObjects[j + 1].objectType = -1;
                rejectList[j] = rejectList[j + 1];
                rejectList[j + 1] = 0;
                ackList[j] = ackList[j + 1];
                ackList[j + 1] = 0;
                objectTypeIsChanged[j] = objectTypeIsChanged[j + 1];
                objectTypeIsChanged[j + 1] = false;
            }
            listSize--;
        }
    }

    if ((goodNumber + badNumber - listSize) > 0)
    {
        int iter = 0;
        Object *objectList = malloc(sizeof(Object) * ARRAY_SIZE);
        if (person.personType - BAD)
        {
            // good
            for (int i = 0; i < toiletNumber; i++)
            {
                if (person.toiletList[i].objectState == BROKEN)
                {
                    int canBeAdd = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (sendObjects[j].objectType == TOILET && sendObjects[j].id == person.toiletList[i].id)
                        {
                            canBeAdd = false;
                            break;
                        }
                    }
                    if (canBeAdd)
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

                    int canBeAdd = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (sendObjects[j].objectType == POT && sendObjects[j].id == person.potList[i].id)
                        {
                            canBeAdd = false;
                            break;
                        }
                    }
                    if (canBeAdd)
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
                    int canBeAdd = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (sendObjects[j].objectType == TOILET && sendObjects[j].id == person.toiletList[i].id)
                        {
                            canBeAdd = false;
                            break;
                        }
                    }
                    if (canBeAdd)
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
                    int canBeAdd = true;
                    for (int j = 0; j < listSize; j++)
                    {
                        if (sendObjects[j].objectType == POT && sendObjects[j].id == person.potList[i].id)
                        {
                            canBeAdd = false;
                            break;
                        }
                    }
                    if (canBeAdd)
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
        for (int i = listSize; i < ARRAY_SIZE; i++)
        {
            rejectList[i] = 0;
            ackList[i] = 0;
            objectTypeIsChanged[i] = false;
        }
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
                updateLamportClock();
                req.priority = person.priority + priority;
                //printf(ANSI_COLOR_YELLOW "[%d]\tPREPARING, %d: Send %s to: %d about %d" ANSI_COLOR_RESET "\n", person.lamportClock, person.id, req.objectState == TREQ ? "TREQ" : "PREQ", j, req.objectId);
                iterationsCounter += 1;
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
            person.avaliableObjectsCount = goodNumber + badNumber - listSize;
        }
        else
        {
            person.avaliableObjectsCount += goodNumber + badNumber - listSize;
        }
    }
    else
    {
        //printf("[%d]\t%s, %d: Receive ACK_ALL with toilet: %d and state: %s\n", person.lamportClock, stateName, person.id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
        person.toiletList[request.objectId - 1].objectState = request.objectState;
        if (person.personType == GOOD)
        {
            person.avaliableObjectsCount += goodNumber + badNumber - listSize;
        }
        else
        {
            person.avaliableObjectsCount += goodNumber + badNumber - listSize;
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
    int resultFalse = true;
    for (int i = 0; i < listSize; i++)
    {
        if (rejectList[i] == 0)
        {
            resultFalse = false;
            break;
            // printf("[%d]\tWAIT_CRITICAL, %d: Remove element from list, current list size: %d\n", person.lamportClock, person.id, tempListSize);
        }
    }

    for (int i = 0; i < listSize; i++)
    {
        if (ackList[i] == (person.goodCount + person.badCount - 1) && !objectTypeIsChanged[i])
        {
            if (sendObjects[i].objectType == TOILET)
            {
                for (int j = 0; j < toiletNumber; j++)
                {
                    if (person.toiletList[j].id == sendObjects[i].id)
                    {
                        printf("[%d]\tWAIT_CRITICAL, %d: ACK for %s %d is given, going to IN_CRITICAL\n", person.lamportClock, person.id, sendObjects[i].objectType == TOILET ? "toilet" : "pot", sendObjects[i].id);
                        *objectId = sendObjects[i].id;
                        *objectType = sendObjects[i].objectType;
                        return true;
                    }
                }
            }
            else
            {
                for (int j = 0; j < potNumber; j++)
                {
                    if (person.potList[j].id == sendObjects[i].id)
                    {
                        printf("[%d]\tWAIT_CRITICAL, %d: ACK for %s %d is given, going to IN_CRITICAL\n", person.lamportClock, person.id, sendObjects[i].objectType == TOILET ? "toilet" : "pot", sendObjects[i].id);
                        *objectId = sendObjects[i].id;
                        *objectType = sendObjects[i].objectType;
                        return true;
                    }
                }
            }
        }
    }
    if (resultFalse)
    {
        //printf("[%d]\tWAIT_CRITICAL, %d: List is empty, going to rest\n", person.lamportClock, person.id);
        return false;
    }
    return -1;
}