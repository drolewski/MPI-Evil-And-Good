#include <unistd.h>
#include <string.h>
#include "structure.h"
#include "functions.h"
#include <pthread.h>

const int toiletNumber = 6;
const int potNumber = 4;
const int goodNumber = 6;
const int badNumber = 6;

Person person;
Object ackObject;
int state = INIT;
int *ackList;
int *rejectList;
int listSize;
Object *sendObjects;
int iterationsCounter;
int iterator;

pthread_mutex_t stateMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lamportMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t listDeletingMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t avaliableObjectsCountMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t listSizeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t iterationsCounterMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t preparingMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t requestThread;
MPI_Datatype MPI_REQ;

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
    //printf(ANSI_COLOR_CYAN "Licz się koleżanko z moim zdaniem: %d" ANSI_COLOR_RESET "\n", person.avaliableObjectsCount);
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
    int nItems = 6;
    int blockLengths[6] = {1, 1, 1, 1, 1, 1};
    MPI_Datatype types[6] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};

    MPI_Aint offsets[6];
    offsets[0] = offsetof(Request, id);
    offsets[1] = offsetof(Request, requestType);
    offsets[2] = offsetof(Request, objectId);
    offsets[3] = offsetof(Request, priority);
    offsets[4] = offsetof(Request, objectState);
    offsets[5] = offsetof(Request, objectType);

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
        person = init(id, toiletList, potList);
        sendObjects = malloc(sizeof(struct Object) * (toiletNumber + potNumber));

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
    free(sendObjects);
    MPI_Type_free(&MPI_REQ);
    MPI_Finalize();
}

void handleStates()
{
    int canGoCritical = false, objectId = -1, objectType = -1, rejectedRest = false;
    while (true)
    {
        pthread_mutex_lock(&stateMutex);
        int lockState = state;
        pthread_mutex_unlock(&stateMutex);
        switch (lockState)
        {
        case INIT:
            pthread_mutex_lock(&iterationsCounterMutex);
            iterationsCounter = 0;
            pthread_mutex_unlock(&iterationsCounterMutex);
            pthread_mutex_lock(&stateMutex);
            state = PREPARING;
            pthread_mutex_unlock(&stateMutex);
            break;
        case PREPARING:
            iterator = preparingState(sendObjects, rejectedRest);
            if (iterator > 0)
            {
                pthread_mutex_lock(&listDeletingMutex);
                rejectList = malloc(sizeof(int) * iterator);
                ackList = malloc(sizeof(int) * iterator);
                memset(ackList, 0, (sizeof(int) * iterator));
                memset(rejectList, 0, (sizeof(int) * iterator));
                pthread_mutex_unlock(&listDeletingMutex);
                pthread_mutex_lock(&listSizeMutex);
                listSize = iterator;
                pthread_mutex_unlock(&listSizeMutex);
                pthread_mutex_lock(&stateMutex);
                state = WAIT_CRITICAL;
                pthread_mutex_unlock(&stateMutex);
            }
            break;
        case WAIT_CRITICAL:

            canGoCritical = waitCriticalState(&objectId, &objectType);
            if (canGoCritical != -1)
            {
                // printf("XD?\n");
                pthread_mutex_lock(&iterationsCounterMutex);
                int tempIterationsCounter = iterationsCounter;
                pthread_mutex_unlock(&iterationsCounterMutex);
                if (tempIterationsCounter <= 0)
                {
                    pthread_mutex_lock(&stateMutex);
                    state = PREPARING;
                    pthread_mutex_unlock(&stateMutex);
                }
            }
            //printf("\ncanGoToCritical: %d\n", canGoCritical);
            if (canGoCritical == true)
            {

                if (objectType == TOILET && objectId > 0)
                {
                    ackObject = person.toiletList[objectId - 1];
                    // printf("\t\ttak wiem noo: %d, typ: %d, %d, person: %d\n", ackObject.objectState, ackObject.objectType, ackObject.id, person.id);
                }
                else if (objectType == POT && objectId > 0)
                {
                    ackObject = person.potList[objectId - 1];
                    // printf("\t\ttak wiem noo: %d, typ: %d, %d, person: %d\n", ackObject.objectState, ackObject.objectType, ackObject.id, person.id);
                }
                rejectedRest = false;
                pthread_mutex_lock(&stateMutex);
                state = IN_CRITICAL;
                pthread_mutex_unlock(&stateMutex);
            }
            else if (canGoCritical == false)
            {
                // printf("\t\ttak wiem noo: %d, %d\n", objectId, objectType);
                rejectedRest = true;
                pthread_mutex_lock(&stateMutex);
                state = REST;
                pthread_mutex_unlock(&stateMutex);
                //printf("\tREST, %d: process is rest\n", person.id);
            }
            //ZWOLNIĆ WSZYSTKO
            break;
        case IN_CRITICAL:
            inCriticalState();
            pthread_mutex_lock(&stateMutex);
            state = AFTER_CRITICAL;
            pthread_mutex_unlock(&stateMutex);
            break;
        case AFTER_CRITICAL:
            afterCriticalState(&ackObject);
            pthread_mutex_lock(&stateMutex);
            state = REST;
            pthread_mutex_unlock(&stateMutex);
            break;
        case REST:
            pthread_mutex_lock(&iterationsCounterMutex);
            int tempRestIterations = iterationsCounter;
            pthread_mutex_unlock(&iterationsCounterMutex);
            if (tempRestIterations <= 0)
            {
                pthread_mutex_lock(&stateMutex);
                state = PREPARING;
                free(ackList);
                free(rejectList);
                pthread_mutex_unlock(&stateMutex);
            }
            break;
        }
    }
    // free(sendObjects);
}

void *handleRequests()
{
    int objectListSize;
    while (true)
    {
        MPI_Status status;
        Request request;
        MPI_Recv(&request, 1, MPI_REQ, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        pthread_mutex_lock(&lamportMutex);
        // printf("ObjectStatus: %d, succes: %d, tag: %d, request priority: %d, lamport: %d, whoami: %d\n", request.objectState, status.MPI_ERROR, status.MPI_TAG, request.priority, person.lamportClock, status.MPI_SOURCE);
        person.lamportClock = request.priority > person.lamportClock ? request.priority + 1 : person.lamportClock + 1;
        pthread_mutex_unlock(&lamportMutex);
        if (status.MPI_ERROR == MPI_SUCCESS)
        {
            pthread_mutex_lock(&stateMutex);
            int lockState = state;
            pthread_mutex_unlock(&stateMutex);
            switch (lockState)
            {
            case PREPARING:
                preparingRequestHandler(request);
                break;
            case WAIT_CRITICAL:
                waitCriticalRequestHandler(request, sendObjects);
                break;
            case REST:
                restRequestHandler(request);
                break;
            }
        }
    }
}

void preparingRequestHandler(Request request)
{
    pthread_mutex_lock(&preparingMutex);
    int receivedId = request.id;
    switch (request.requestType)
    {
    case PREQ:
        request.id = person.id;
        request.requestType = PACK;
        request.objectType = POT;
        request.objectState = request.objectState;
        request.objectId = request.objectId;
        updateLamportClock();
        request.priority = request.priority;
        printf("\tPREPARING, %d: Send PACK to: %d about %d\n", person.id, receivedId, request.objectId);
        MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
        break;
    case TREQ:
        request.id = person.id;
        request.objectId = request.objectId;
        request.requestType = TACK;
        request.objectType = TOILET;
        request.objectState = request.objectState;
        updateLamportClock();
        request.priority = request.priority;
        printf("\tPREPARING, %d: Send TACK to: %d about %d\n", person.id, receivedId, request.objectId);
        MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
        break;
    case ACKALL:
        updateLists(request, "PREPARING");
        break;
    case PACK:

        break;
    case TACK:
        break;
    case REJECT:
        break;
    default:
        printf(ANSI_COLOR_MAGENTA "PREPARING - request: %d, sendertId: %d, objectID: %d, objectType: %d, objectState: %d, priority: %d, my Priority: %d, my id: %d" ANSI_COLOR_RESET "\n", request.requestType, request.id, request.objectId, request.objectType, request.objectState, request.priority, person.priority, person.id);
        //printf("\tPREPARING, %d: Received ignore message.\n", person.id);
        break;
    }
    pthread_mutex_unlock(&preparingMutex);
}

void waitCriticalRequestHandler(Request request, Object *objectList)
{
    int tempListSize;
    int receivedId = request.id;
    switch (request.requestType)
    {
    case PREQ:
        // printf("\tWAIT_CRITICAL, %d: Receive PREQ from id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
        // printf("receivedId: %d\n", receivedId);
        if ((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount))
        {
            request.id = person.id;
            request.requestType = PACK;
            request.objectId = request.objectId;
            request.objectState = request.objectState;
            request.objectType = request.objectType;
            updateLamportClock();
            request.priority = request.priority;
            printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
            MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
        }
        else
        {
            int arePotsInList = false;
            pthread_mutex_lock(&listSizeMutex);
            int tempListSize = listSize;
            pthread_mutex_unlock(&listSizeMutex);
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
                request.id = person.id;
                request.requestType = PACK;
                request.objectId = request.objectId;
                request.objectState = request.objectState;
                request.objectType = request.objectType;
                updateLamportClock();
                request.priority = request.priority;
                printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
            }
            else
            {
                int arePotsWithIdInList = false;
                pthread_mutex_lock(&listSizeMutex);
                int tempListSize = listSize;
                pthread_mutex_unlock(&listSizeMutex);
                for (int i = 0; i < tempListSize; i++)
                {
                    if (objectList[i].objectType == POT && objectList[i].id == request.objectId)
                    {
                        arePotsWithIdInList = true;

                        if (person.priority > request.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                        {
                            request.id = person.id;
                            request.requestType = PACK;
                            request.objectId = request.objectId;
                            request.objectState = request.objectState;
                            request.objectType = request.objectType;
                            updateLamportClock();
                            request.priority = request.priority;
                            printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                            MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                            pthread_mutex_lock(&listDeletingMutex);
                            rejectList[i] += 1;
                            pthread_mutex_unlock(&listDeletingMutex);
                        }
                        else if (person.priority < request.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                        {
                            request.id = person.id;
                            request.requestType = REJECT;
                            request.objectId = request.objectId;
                            request.objectState = request.objectState;
                            request.objectType = request.objectType;
                            updateLamportClock();
                            request.priority = request.priority;
                            printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                            MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                        }
                        else //równe priorytety
                        {
                            if (person.id > request.id)
                            {
                                request.id = person.id;
                                request.requestType = REJECT;
                                request.objectId = request.objectId;
                                request.objectState = request.objectState;
                                request.objectType = request.objectType;
                                updateLamportClock();
                                request.priority = request.priority;
                                printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                                MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                            }
                            else
                            {
                                request.id = person.id;
                                request.requestType = PACK;
                                request.objectId = request.objectId;
                                request.objectState = request.objectState;
                                request.objectType = request.objectType;
                                updateLamportClock();
                                request.priority = request.priority;
                                printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                                MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                                pthread_mutex_lock(&listDeletingMutex);
                                rejectList[i] += 1;
                                pthread_mutex_unlock(&listDeletingMutex);
                            }
                        }
                    }
                }
                if (arePotsWithIdInList == false)
                {
                    request.id = person.id;
                    request.requestType = PACK;
                    request.objectId = request.objectId;
                    request.objectState = request.objectState;
                    request.objectType = request.objectType;
                    updateLamportClock();
                    request.priority = request.priority;
                    printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                    MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                }
            }
        }
        // printf("Lamport: %d, ReceivedId: %d, PersonId: %d, objectType: %d, ObjectState: %d, personType: %d, Person priorytet: %d, request priority: %d\n", person.lamportClock, receivedId, person.id, request.objectType, request.objectState, person.personType, person.priority, request.priority);
        break;
    case TREQ:
        // printf("\tWAIT_CRITICAL, %d: Receive TREQ from id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
        if ((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount))
        {
            request.id = person.id;
            request.requestType = TACK;
            request.objectId = request.objectId;
            request.objectState = request.objectState;
            request.objectType = request.objectType;
            updateLamportClock();
            request.priority = request.priority;
            printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
            MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
        }
        else
        {
            int areToiletsInList = false;
            pthread_mutex_lock(&listSizeMutex);
            int tempListSize = listSize;
            pthread_mutex_unlock(&listSizeMutex);
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
                request.id = person.id;
                request.requestType = TACK;
                request.objectId = request.objectId;
                request.objectState = request.objectState;
                request.objectType = request.objectType;
                updateLamportClock();
                request.priority = request.priority;
                printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
            }
            else
            {
                int areToiletsWithIdInList = false;
                for (int i = 0; i < listSize; i++)
                {
                    if (objectList[i].objectType == TOILET && objectList[i].id == request.objectId)
                    {
                        areToiletsWithIdInList = true;

                        if (person.priority > request.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                        {
                            request.id = person.id;
                            request.requestType = TACK;
                            request.objectId = request.objectId;
                            request.objectState = request.objectState;
                            request.objectType = request.objectType;
                            updateLamportClock();
                            request.priority = request.priority;
                            printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                            MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                            pthread_mutex_lock(&listDeletingMutex);
                            rejectList[i] += 1;
                            pthread_mutex_unlock(&listDeletingMutex);
                        }
                        else if (person.priority < request.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                        {
                            request.id = person.id;
                            request.requestType = REJECT;
                            request.objectId = request.objectId;
                            request.objectState = request.objectState;
                            request.objectType = request.objectType;
                            updateLamportClock();
                            request.priority = request.priority;
                            printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                            MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                        }
                        else //równe priorytety
                        {
                            if (person.id > request.id)
                            {
                                request.id = person.id;
                                request.requestType = REJECT;
                                request.objectId = request.objectId;
                                request.objectState = request.objectState;
                                request.objectType = request.objectType;
                                updateLamportClock();
                                request.priority = request.priority;
                                printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                                MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                            }
                            else
                            {
                                request.id = person.id;
                                request.requestType = TACK;
                                request.objectId = request.objectId;
                                request.objectState = request.objectState;
                                request.objectType = request.objectType;
                                updateLamportClock();
                                request.priority = request.priority;
                                printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                                MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                                pthread_mutex_lock(&listDeletingMutex);
                                rejectList[i] += 1;
                                pthread_mutex_unlock(&listDeletingMutex);
                            }
                        }
                    }
                }
                if (areToiletsWithIdInList == false)
                {
                    request.id = person.id;
                    request.requestType = TACK;
                    request.objectId = request.objectId;
                    request.objectState = request.objectState;
                    request.objectType = request.objectType;
                    updateLamportClock();
                    request.priority = request.priority;
                    printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
                    MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                }
            }
        }
        break;
    case ACKALL:
        updateLists(request, "WAIT_CRITICAL");

        if (!((receivedId > person.goodCount && person.id <= person.goodCount) || (receivedId <= person.goodCount && person.id > person.goodCount)))
        {
            pthread_mutex_lock(&listSizeMutex);
            int tempListSize = listSize;
            pthread_mutex_unlock(&listSizeMutex);

            pthread_mutex_lock(&listDeletingMutex);
            // printf(ANSI_COLOR_MAGENTA "skad: %d, objectType: %d, priorytet: %d" ANSI_COLOR_RESET "\n", request.id, request.objectType, request.priority);
            for (int i = 0; i < tempListSize; i++)
            {
                ackList[i] += 1;
                pthread_mutex_lock(&iterationsCounterMutex);
                iterationsCounter -= 1;
                pthread_mutex_unlock(&iterationsCounterMutex);
            }
            pthread_mutex_unlock(&listDeletingMutex);
        }
        break;
    case PACK:
        printf("\tWAIT_CRITICAL, %d: Receive PACK from: %d\n", person.id, receivedId);
        pthread_mutex_lock(&listSizeMutex);
        tempListSize = listSize;
        pthread_mutex_unlock(&listSizeMutex);
        pthread_mutex_lock(&listDeletingMutex);
        // printf(ANSI_COLOR_MAGENTA "skad: %d, objectType: %d, priorytet: %d" ANSI_COLOR_RESET "\n", request.id, request.objectType, request.priority);
        for (int i = 0; i < tempListSize; i++)
        {
            if (objectList[i].objectType == POT && objectList[i].id == request.objectId)
            {
                ackList[i] += 1;
                pthread_mutex_lock(&iterationsCounterMutex);
                iterationsCounter -= 1;
                pthread_mutex_unlock(&iterationsCounterMutex);
            }
        }
        pthread_mutex_unlock(&listDeletingMutex);
        break;
    case TACK:
        printf("\tWAIT_CRITICAL, %d: Receive TACK from: %d about: %d\n", person.id, receivedId, request.objectId);
        pthread_mutex_lock(&listSizeMutex);
        tempListSize = listSize;
        pthread_mutex_unlock(&listSizeMutex);
        pthread_mutex_lock(&listDeletingMutex);
        for (int i = 0; i < tempListSize; i++)
        {
            // printf(ANSI_COLOR_MAGENTA "skad: %d, objectType: %d, priorytet: %d" ANSI_COLOR_RESET "\n", request.id, request.objectType, request.priority);
            if (objectList[i].objectType == TOILET && objectList[i].id == request.objectId)
            {
                ackList[i] += 1;
                pthread_mutex_lock(&iterationsCounterMutex);
                iterationsCounter -= 1;
                pthread_mutex_unlock(&iterationsCounterMutex);
            }
        }
        pthread_mutex_unlock(&listDeletingMutex);
        break;
    case REJECT:
        printf("\tWAIT_CRITICAL, %d: Receive REJECT from: %d\n", person.id, receivedId);
        pthread_mutex_lock(&listSizeMutex);
        tempListSize = listSize;
        pthread_mutex_unlock(&listSizeMutex);
        pthread_mutex_lock(&listDeletingMutex);
        for (int i = 0; i < tempListSize; i++)
        {
            if (objectList[i].id == request.objectId)
            {
                rejectList[i] += 1;
                // person.priority = request.priority;
                pthread_mutex_lock(&iterationsCounterMutex);
                iterationsCounter -= 1;
                pthread_mutex_unlock(&iterationsCounterMutex);
            }
            pthread_mutex_unlock(&listDeletingMutex);
        }
        break;
    default:
        //printf("\tWAIT_CRITICAL, %d: Received ignore message.\n", person.id);
        break;
    }
}

void restRequestHandler(Request request)
{
    int receivedId = request.id;
    switch (request.requestType)
    {
    case PREQ:
        request.id = person.id;
        request.requestType = PACK;
        request.objectId = request.objectId;
        request.objectState = request.objectState;
        request.objectType = request.objectType;
        updateLamportClock();
        printf("\tREST, %d: SEND PACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
        MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
        break;
    case TREQ:
        request.id = person.id;
        request.requestType = TACK;
        request.objectId = request.objectId;
        request.objectState = request.objectState;
        request.objectType = request.objectType;
        updateLamportClock();
        printf("\tREST, %d: SEND TACK to id: %d and objectId: %d\n", person.id, receivedId, request.objectId);
        MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
        break;
    case ACKALL:
        updateLists(request, "REST");
        pthread_mutex_lock(&iterationsCounterMutex);
        iterationsCounter -= iterator;
        pthread_mutex_unlock(&iterationsCounterMutex);
        break;
    case PACK:
        pthread_mutex_lock(&iterationsCounterMutex);
        iterationsCounter--;
        pthread_mutex_unlock(&iterationsCounterMutex);
        break;
    case TACK:
        pthread_mutex_lock(&iterationsCounterMutex);
        iterationsCounter--;
        pthread_mutex_unlock(&iterationsCounterMutex);
        break;
    default:
        printf("\tREST, %d: Received ignore message.\n", person.id);
        break;
    }
}

void inCriticalState()
{
    printf(ANSI_COLOR_CYAN "\tIN_CRITICAL, %d: process is in cricital section" ANSI_COLOR_RESET "\n", person.id);
    waitRandomTime(person.id);
}

void afterCriticalState(Object *object)
{
    Request request;
    request.id = person.id;
    request.requestType = ACKALL;
    request.objectId = object->id;
    request.objectState = object->objectState == BROKEN ? REPAIRED : BROKEN;
    //printf(ANSI_COLOR_MAGENTA "Ja mam ten idealny stan: %s" ANSI_COLOR_RESET "\n", request.objectState == BROKEN ? "Broken" : "Repaired");
    request.objectType = object->objectType;
    request.priority = person.lamportClock;
    updateLists(request, "AFTER_CRITICAL");
    for (int i = 0; i <= (person.goodCount + person.badCount); i++)
    {
        updateLamportClock();
        printf("\tAFTER_CRITICAL, %d: SEND ACKALL to id: %d about objectId: %d\n", person.id, i, request.objectId);
        MPI_Send(&request, 1, MPI_REQ, i, ACKALL, MPI_COMM_WORLD);
    }
}

int preparingState(Object *objectList, int rejectedRest)
{
    pthread_mutex_lock(&avaliableObjectsCountMutex);
    int availableObjectsCount = person.avaliableObjectsCount;
    // printf(ANSI_COLOR_MAGENTA "PREPARING - Licz się koleżanko z moim zdaniem: %d jestem s**ą o imieniu: %s" ANSI_COLOR_RESET "\n", person.avaliableObjectsCount, person.personType == BAD ? "bad" : "good");
    pthread_mutex_unlock(&avaliableObjectsCountMutex);
    if (availableObjectsCount > 0)
    {
        int iterator = 0;
        if (person.personType - BAD)
        {
            // good
            for (int i = 0; i < toiletNumber; i++)
            {
                if (person.toiletList[i].objectState == BROKEN)
                {
                    objectList[iterator] = person.toiletList[i];
                    iterator++;
                }
            }
            for (int i = 0; i < potNumber; i++)
            {
                if (person.potList[i].objectState == BROKEN)
                {

                    objectList[iterator] = person.potList[i];
                    iterator++;
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
                    objectList[iterator] = person.toiletList[i];
                    iterator++;
                }
            }
            for (int i = 0; i < potNumber; i++)
            {
                if (person.potList[i].objectState == REPAIRED)
                {
                    objectList[iterator] = person.potList[i];
                    iterator++;
                }
            }
        }

        pthread_mutex_lock(&preparingMutex);
        sendRequestForObjects(sendObjects, iterator, rejectedRest);
        pthread_mutex_unlock(&preparingMutex);

        return iterator;
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
        // printf(ANSI_COLOR_YELLOW "Ja mam ten idealny stan: %s" ANSI_COLOR_RESET "\n", req.objectState == BROKEN ? "Broken" : req.objectState == REPAIRED ? "repaired" : "smieci");
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
                printf(ANSI_COLOR_YELLOW "\tPREPARING, %d: Send %s to: %d about %d" ANSI_COLOR_RESET "\n", person.id, req.objectState == TREQ ? "TREQ" : "PREQ", j, req.objectId);
                pthread_mutex_lock(&iterationsCounterMutex);
                iterationsCounter += 1;
                pthread_mutex_unlock(&iterationsCounterMutex);
                MPI_Send(&req, 1, MPI_REQ, j, req.requestType, MPI_COMM_WORLD);
            }
        }
    }
}

void updateLists(Request request, char *stateName)
{
    int receivedId = request.id;
    pthread_mutex_lock(&avaliableObjectsCountMutex);
    if (request.objectType == POT)
    {
        //printf("\t%s, %d: Receive ACK_ALL with pot: %d and state: %s\n", stateName, person.id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
        person.potList[request.objectId - 1].objectState = request.objectState;
        //printf(ANSI_COLOR_GREEN "Ja mam ten idealny stan: %s" ANSI_COLOR_RESET "\n", request.objectState == BROKEN ? "Broken" : "Repaired");
        if (person.personType == GOOD)
        {
            person.avaliableObjectsCount += request.objectState - BROKEN ? -1 : 1;
            //printf(ANSI_COLOR_RED "JA: %d, OBj: %d, no dawaj: %d" ANSI_COLOR_RESET "\n", person.personType, request.objectType, request.objectState - BROKEN ? 1 : -1);
        }
        else
        {
            person.avaliableObjectsCount += request.objectState - BROKEN ? 1 : -1;
            //printf(ANSI_COLOR_RED "JA: %d, OBj: %d, no dawaj: %d" ANSI_COLOR_RESET "\n", person.personType, request.objectType, request.objectState - BROKEN ? 1 : -1);
        }
    }
    else
    {
        //printf("\t%s, %d: Receive ACK_ALL with toilet: %d and state: %s\n", stateName, person.id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
        person.toiletList[request.objectId - 1].objectState = request.objectState;
        //printf(ANSI_COLOR_GREEN "Ja mam ten idealny stan: %s" ANSI_COLOR_RESET "\n", request.objectState == BROKEN ? "Broken" : "Repaired");
        if (person.personType == GOOD)
        {
            person.avaliableObjectsCount += request.objectState - BROKEN ? -1 : 1;
            //printf(ANSI_COLOR_RED "JA: %d, OBj: %d, no dawaj: %d" ANSI_COLOR_RESET "\n", person.personType, request.objectType, request.objectState - BROKEN ? 1 : -1);
        }
        else
        {
            person.avaliableObjectsCount += request.objectState - BROKEN ? 1 : -1;
            //printf(ANSI_COLOR_RED "JA: %d, OBj: %d, no dawaj: %d" ANSI_COLOR_RESET "\n", person.personType, request.objectType, request.objectState - BROKEN ? 1 : -1);
        }
    }
    //printf(ANSI_COLOR_CYAN "%s - Licz się koleżanko z moim zdaniem: %d jestem s**ą o imieniu: %s" ANSI_COLOR_RESET "\n", stateName, person.avaliableObjectsCount, person.personType == BAD ? "bad" : "good");
    pthread_mutex_unlock(&avaliableObjectsCountMutex);
}

void updateLamportClock()
{
    pthread_mutex_lock(&lamportMutex);
    person.lamportClock += 1;
    pthread_mutex_unlock(&lamportMutex);
}

int waitCriticalState(int *objectId, int *objectType)
{

    pthread_mutex_lock(&listSizeMutex);
    int tempListSize = listSize;
    pthread_mutex_unlock(&listSizeMutex);
    for (int i = 0; i < tempListSize; i++)
    {
        pthread_mutex_lock(&listDeletingMutex);
        int tempRejectListValue = rejectList[i];
        pthread_mutex_unlock(&listDeletingMutex);
        if (tempRejectListValue > 0)
        {
            // delete from array
            pthread_mutex_lock(&listDeletingMutex);
            for (int j = i; j < tempListSize - 1; j++)
            {
                sendObjects[j] = sendObjects[j + 1];
                rejectList[j] = rejectList[j + 1];
                ackList[j] = ackList[j + 1];
            }
            tempListSize -= 1;
            printf("\tWAIT_CRITICAL, %d: Remove element from list, current list size: %d\n", person.id, tempListSize);
            pthread_mutex_unlock(&listDeletingMutex);

            pthread_mutex_lock(&listSizeMutex);
            listSize = tempListSize;
            pthread_mutex_unlock(&listSizeMutex);
        }
    }

    for (int i = 0; i < tempListSize; i++)
    {
        pthread_mutex_lock(&listDeletingMutex);
        int tempAckListValue = ackList[i];
        pthread_mutex_unlock(&listDeletingMutex);
        // printf("ackList: %d, reszta gowna: %d\n", ackList[i],person.goodCount + person.badCount - 1);
        if (tempAckListValue == (person.goodCount + person.badCount - 1))
        {
            //printf("\tWAIT_CRITICAL, %d: ACK for %s %d is given, going to IN_CRITICAL\n", person.id, sendObjects[i].objectType == TOILET ? "toilet" : "pot", sendObjects[i].id);
            *objectId = sendObjects[i].id;
            *objectType = sendObjects[i].objectType;
            return true;
        }
    }

    pthread_mutex_lock(&listSizeMutex);
    tempListSize = listSize;
    pthread_mutex_unlock(&listSizeMutex);
    if (tempListSize == 0)
    {
        printf("\tWAIT_CRITICAL, %d: List is empty, going to rest\n", person.id);
        return false;
    }
    return -1;
}