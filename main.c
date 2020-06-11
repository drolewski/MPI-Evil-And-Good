#include <unistd.h>
#include <string.h>
#include "structure.h"
#include "functions.h"

const int toiletNumber = 2;
const int potNumber = 1;
const int goodNumber = 1;
const int badNumber = 2;

MPI_Datatype MPI_REQ;
MPI_Datatype MPI_ARequest;

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
        potList[i] = pot;
    }

    for (int i = 0; i < toiletNumber; i++)
    {
        Object toilet;
        toilet.id = i + 1;
        toilet.noInList = i;
        toilet.objectState = REPAIRED;
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
    printf("Process: %d is waiting: %f\n", id, seconds);
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

    nItems = 6;
    int aBlockLenghts[6] = {1, 1, 1, 1, 1, 1};
    MPI_Datatype aTypes[6] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};

    MPI_Aint aOffsets[6];
    aOffsets[0] = offsetof(ARequest, id);
    aOffsets[1] = offsetof(ARequest, requestType);
    aOffsets[2] = offsetof(ARequest, objectId);
    aOffsets[3] = offsetof(ARequest, priority);
    aOffsets[4] = offsetof(ARequest, objectState);
    aOffsets[5] = offsetof(ARequest, objectType);

    MPI_Type_create_struct(nItems, aBlockLenghts, aOffsets, aTypes, &MPI_ARequest);
    MPI_Type_commit(&MPI_ARequest);
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int size, rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    setupStructures();

    MPI_Status status;
    if (rank == 0)
    {
        for (int i = 1; i <= (goodNumber + badNumber); i++)
        {
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
        Person person = init(id, toiletList, potList);
        printf("Process: %d is Person: %d, %s\n", rank, person.id, person.personType - BAD ? "good" : "bad");
        MPI_Send(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD);

        MPI_Recv(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD, &status);

        waitRandomTime(id);

        Object *sendObjects = malloc(sizeof(struct Object) * (toiletNumber + potNumber));
        int objectListSize = preparing(&person, sendObjects);
        int objectId = -1;
        int objectType = -1;
        int canGoCritical = waitCritical(&person, sendObjects, objectListSize, &objectId, &objectType);

        if (canGoCritical)
        {
            inCritical(&person);

            Object object;
            if (objectType == TOILET && objectId > 0)
            {
                object = person.toiletList[objectId - 1];
            }
            else if (objectType == POT && objectId > 0)
            {
                object = person.potList[objectId - 1];
            }

            afterCritical(&person, &object);
            rest(&person);
        }
        else
        {
            rest(&person);
        }
    }

    MPI_Finalize();
}

int preparing(Person *person, Object *objectList)
{
    while (true)
    {
        if (person->avaliableObjectsCount > 0)
        {
            int iterator = 0;
            if (person->personType - 100)
            {
                // good
                for (int i = 0; i < toiletNumber; i++)
                {
                    if (person->toiletList[i].objectState == BROKEN)
                    {
                        time_t tt;
                        int quantum = time(&tt);
                        srand(quantum + person->id);
                        double priority = rand() % 10;
                        Request req;
                        req.id = person->id;
                        req.objectId = person->toiletList[i].id;
                        req.priority = person->lamportClock + priority;
                        req.requestType = TREQ;
                        for (int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if (i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_REQ, i, TREQ, MPI_COMM_WORLD);
                            }
                        }
                        objectList[iterator] = person->toiletList[i];
                        iterator++;
                    }
                }
                for (int i = 0; i < potNumber; i++)
                {
                    if (person->potList[i].objectState == BROKEN)
                    {
                        time_t tt;
                        int quantum = time(&tt);
                        srand(quantum + person->id);
                        double priority = rand() % 10;
                        Request req;
                        req.id = person->id;
                        req.objectId = person->potList[i].id;
                        req.priority = person->lamportClock + priority;
                        req.requestType = PREQ;
                        for (int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if (i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_REQ, i, PREQ, MPI_COMM_WORLD);
                            }
                        }
                        objectList[iterator] = person->potList[i];
                        iterator++;
                    }
                }
                person->lamportClock += 1;
            }
            else
            {
                // bad
                for (int i = 0; i < toiletNumber; i++)
                {
                    if (person->toiletList[i].objectState == REPAIRED)
                    {
                        time_t tt;
                        int quantum = time(&tt);
                        srand(quantum + person->id);
                        double priority = rand() % 10;
                        Request req;
                        req.id = person->id;
                        req.objectId = person->toiletList[i].id;
                        req.priority = person->lamportClock + priority;
                        req.requestType = TREQ;
                        for (int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if (i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_REQ, i, TREQ, MPI_COMM_WORLD);
                            }
                        }
                        objectList[iterator] = person->toiletList[i];
                        iterator++;
                    }
                }
                for (int i = 0; i < potNumber; i++)
                {
                    if (person->potList[i].objectState == REPAIRED)
                    {
                        time_t tt;
                        int quantum = time(&tt);
                        srand(quantum + person->id);
                        double priority = rand() % 10;
                        Request req;
                        req.id = person->id;
                        req.objectId = person->potList[i].id;
                        req.priority = person->lamportClock + priority;
                        req.requestType = PREQ;
                        for (int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if (i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_REQ, i, PREQ, MPI_COMM_WORLD);
                            }
                        }
                        objectList[iterator] = person->potList[i];
                        iterator++;
                    }
                }
                person->lamportClock += 1;
            }
            return iterator;
        }
        else
        {
            MPI_Status status;
            Request request;
            MPI_Recv(&request, sizeof(Request), MPI_REQ, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            person->lamportClock = request.priority > person->lamportClock ? request.priority + 1 : person->lamportClock + 1;
            if (status.MPI_ERROR == MPI_SUCCESS)
            {
                int receivedId = request.id;
                switch (request.requestType)
                {
                case PREQ:
                    request.id = person->id;
                    request.requestType = PACK;
                    person->lamportClock += 1;
                    printf("\tPREPARING, %d: Send PACK to: %d\n", person->id, receivedId);
                    MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                    break;
                case TREQ:
                    request.id = person->id;
                    request.requestType = TACK;
                    person->lamportClock += 1;
                    printf("\tPREPARING, %d: Send TACK to: %d\n", person->id, receivedId);
                    MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                    break;
                case ACKALL:
                    if (request.objectType == POT)
                    {
                        printf("\tPREPARING, %d: Receive ACK_ALL with pot: %d and state: %s\n", person->id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
                        person->potList[request.objectId - 1].objectState = request.objectState;
                        if (person->personType == GOOD)
                        {
                            person->avaliableObjectsCount += request.objectState - BROKEN ? -1 : 1;
                        }
                        else
                        {
                            person->avaliableObjectsCount += request.objectState - BROKEN ? 1 : -1;
                        }
                    }
                    else
                    {
                        printf("\tPREPARING, %d: Receive ACK_ALL with toilet: %d and state: %s\n", person->id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
                        person->toiletList[request.objectId - 1].objectState = request.objectState;
                        if (person->personType == GOOD)
                        {
                            person->avaliableObjectsCount += request.objectState - BROKEN ? -1 : 1;
                        }
                        else
                        {
                            person->avaliableObjectsCount += request.objectState - BROKEN ? 1 : -1;
                        }
                    }
                    break;
                default:
                    printf("\tPREPARING, %d: Received ignore message.\n", person->id);
                    break;
                }
            }
        }
    }
    return -1;
}

int waitCritical(Person *person, Object *objectList, int listSize, int *objectId, int *objectType)
{
    int *ackList = malloc(sizeof(int) * listSize);
    memset(ackList, 0, (sizeof(int) * listSize));
    int *rejectList = malloc(sizeof(int) * listSize);
    memset(rejectList, 0, (sizeof(int) * listSize));
    while (true)
    {
        MPI_Status status;
        Request request;
        MPI_Recv(&request, sizeof(Request), MPI_REQ, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        person->lamportClock = request.priority > person->lamportClock ? request.priority + 1 : person->lamportClock + 1;
        if (status.MPI_ERROR == MPI_SUCCESS)
        {
            int receivedId = request.id;
            switch (request.requestType)
            {
            case PREQ:
                printf("\tWAIT_CRITICAL, %d: Receive PREQ from id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                if ((receivedId > person->goodCount && person->id <= person->goodCount) || (receivedId <= person->goodCount && person->id > person->goodCount))
                {
                    request.id = person->id;
                    request.requestType = PACK;
                    person->lamportClock += 1;
                    printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                    MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                }
                else
                {
                    int arePotsInList = false;
                    for (int i = 0; i < listSize; i++)
                    {
                        if (objectList[i].objectType == POT)
                        {
                            arePotsInList = true;
                            break;
                        }
                    }

                    if (arePotsInList == false)
                    {
                        request.id = person->id;
                        request.requestType = PACK;
                        person->lamportClock += 1;
                        printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                        MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                    }
                    else
                    {
                        int arePotsWithIdInList = false;
                        for (int i = 0; i < listSize; i++)
                        {
                            if (objectList[i].objectType == POT && objectList[i].id == request.objectId)
                            {
                                arePotsWithIdInList = true;

                                if (person->priority > request.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                                {
                                    request.id = person->id;
                                    request.requestType = PACK;
                                    person->lamportClock += 1;
                                    printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                    MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                                    rejectList[i] += 1;
                                }
                                else if (person->priority < request.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                                {
                                    request.id = person->id;
                                    request.requestType = REJECT;
                                    person->lamportClock += 1;
                                    printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                    MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                                }
                                else //równe priorytety
                                {
                                    if (person->id > request.id)
                                    {
                                        request.id = person->id;
                                        request.requestType = REJECT;
                                        person->lamportClock += 1;
                                        printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                        MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                                    }
                                    else
                                    {
                                        request.id = person->id;
                                        request.requestType = PACK;
                                        person->lamportClock += 1;
                                        printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                        MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                                        rejectList[i] += 1;
                                    }
                                }
                            }
                        }
                        if (arePotsWithIdInList == false)
                        {
                            request.id = person->id;
                            request.requestType = PACK;
                            person->lamportClock += 1;
                            printf("\tWAIT_CRITICAL, %d: SEND PACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                            MPI_Send(&request, 1, MPI_REQ, receivedId, PACK, MPI_COMM_WORLD);
                        }
                    }
                }
                break;
            case TREQ:
                printf("\tWAIT_CRITICAL, %d: Receive TREQ from id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                if ((receivedId > person->goodCount && person->id <= person->goodCount) || (receivedId <= person->goodCount && person->id > person->goodCount))
                {
                    request.id = person->id;
                    request.requestType = TACK;
                    person->lamportClock += 1;
                    printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                    MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                }
                else
                {
                    int areToiletsInList = false;
                    for (int i = 0; i < listSize; i++)
                    {
                        if (objectList[i].objectType == TOILET)
                        {
                            areToiletsInList = true;
                            break;
                        }
                    }

                    if (areToiletsInList == false)
                    {
                        request.id = person->id;
                        request.requestType = TACK;
                        person->lamportClock += 1;
                        printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
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

                                if (person->priority > request.priority) // request ma wyższy priorytet, tj. niższą wartość zmiennej priority
                                {
                                    request.id = person->id;
                                    request.requestType = TACK;
                                    person->lamportClock += 1;
                                    printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                    MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                                    rejectList[i] += 1;
                                }
                                else if (person->priority < request.priority) // request ma niższy priorytet, tj. wyższą wartość zmiennej priority
                                {
                                    request.id = person->id;
                                    request.requestType = REJECT;
                                    person->lamportClock += 1;
                                    printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                    MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                                }
                                else //równe priorytety
                                {
                                    if (person->id > request.id)
                                    {
                                        request.id = person->id;
                                        request.requestType = REJECT;
                                        person->lamportClock += 1;
                                        printf("\tWAIT_CRITICAL, %d: SEND REJECT to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                        MPI_Send(&request, 1, MPI_REQ, receivedId, REJECT, MPI_COMM_WORLD);
                                    }
                                    else
                                    {
                                        request.id = person->id;
                                        request.requestType = TACK;
                                        person->lamportClock += 1;
                                        printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                                        MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                                        rejectList[i] += 1;
                                    }
                                }
                            }
                        }
                        if (areToiletsWithIdInList == false)
                        {
                            request.id = person->id;
                            request.requestType = TACK;
                            person->lamportClock += 1;
                            printf("\tWAIT_CRITICAL, %d: SEND TACK to id: %d and objectId: %d\n", person->id, receivedId, request.objectId);
                            MPI_Send(&request, 1, MPI_REQ, receivedId, TACK, MPI_COMM_WORLD);
                        }
                    }
                }
                break;
            case ACKALL:
                if (request.objectType == POT)
                {
                    printf("\tWAIT_CRITICAL, %d: Receive ACK_ALL with pot: %d and state: %s\n", person->id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
                    person->potList[request.objectId - 1].objectState = request.objectState;
                    if (person->personType == GOOD)
                    {
                        person->avaliableObjectsCount += request.objectState - BROKEN ? -1 : 1;
                    }
                    else
                    {
                        person->avaliableObjectsCount += request.objectState - BROKEN ? 1 : -1;
                    }
                }
                else
                {
                    printf("\tWAIT_CRITICAL, %d: Receive ACK_ALL with toilet: %d and state: %s\n", person->id, receivedId, request.objectState - BROKEN ? "repaired" : "broken");
                    person->toiletList[request.objectId - 1].objectState = request.objectState;
                    if (person->personType == GOOD)
                    {
                        person->avaliableObjectsCount += request.objectState - BROKEN ? -1 : 1;
                    }
                    else
                    {
                        person->avaliableObjectsCount += request.objectState - BROKEN ? 1 : -1;
                    }
                }

                if (!((receivedId > person->goodCount && person->id <= person->goodCount) || (receivedId <= person->goodCount && person->id > person->goodCount)))
                {
                    for (int i = 0; i < listSize; i++)
                    {
                        if (request.objectType == objectList[i].objectType)
                        {
                            if (request.objectId == objectList[i].id)
                            {
                                ackList[i] += 1;
                            }
                            else
                            {
                                rejectList[i] += 1;
                            }
                        }
                    }
                }
                break;
            case PACK:
                printf("\tWAIT_CRITICAL, %d: Receive PACK from: %d\n", person->id, receivedId);
                for (int i = 0; i < listSize; i++)
                {
                    if (objectList[i].objectType == POT && objectList[i].id == request.objectId)
                    {
                        ackList[i] += 1;
                    }
                }
                break;
            case TACK:
                printf("\tWAIT_CRITICAL, %d: Receive TACK from: %d\n", person->id, receivedId);
                for (int i = 0; i < listSize; i++)
                {
                    if (objectList[i].objectType == TOILET && objectList[i].id == request.objectId)
                    {
                        ackList[i] += 1;
                    }
                }
                break;
            case REJECT:
                printf("\tWAIT_CRITICAL, %d: Receive REJECT from: %d\n", person->id, receivedId);
                for (int i = 0; i < listSize; i++)
                {
                    if (objectList[i].id == request.objectId)
                    {
                        rejectList[i] += 1;
                        person->priority = request.priority;
                    }
                }
                break;
            default:
                printf("\tWAIT_CRITICAL, %d: Received ignore message.\n", person->id);
                break;
            }
        }
        for (int i = 0; i < listSize; i++)
        {
            if (rejectList[i] > 0)
            {
                // delete from array
                for (int j = i; j < listSize - 1; j++)
                {
                    objectList[j] = objectList[j + 1];
                    rejectList[j] = rejectList[j + 1];
                    ackList[j] = ackList[j + 1];
                }
                listSize -= 1;
                printf("\tWAIT_CRITICAL, %d: Remove element from list, current list size: %d\n", person->id, listSize);
            }
        }

        for (int i = 0; i < listSize; i++)
        {
            if (ackList[i] == (person->goodCount + person->badCount - 1))
            {
                printf("\tWAIT_CRITICAL, %d: ACK for %s %d is given, going to IN_CRITICAL\n", person->id, objectList[i].objectType == TOILET ? "toilet" : "pot", objectList[i].id);
                objectId = objectList[i].id;
                objectType = objectList[i].objectType;
                return true;
            }
        }

        if (listSize == 0)
        {
            printf("\tWAIT_CRITICAL, %d: List is empty, going to rest\n", person->id);
            return false;
        }
    }
}

void rest(Person *person)
{
    printf("\tREST, %d: process is rest\n", person->id);
    waitRandomTime(person->id);
}

void inCritical(Person *person)
{
    printf("\tIN_CRITICAL, %d: process is in cricital section\n", person->id);
    waitRandomTime(person->id);
}

void afterCritical(Person *person, Object *object)
{
    Request request;
    for (int i = 0; i <= (person->goodCount + person->badCount); i++)
    {
        request.id = person->id;
        request.requestType = ACKALL;
        request.objectId = object->id;
        request.objectState = object->objectState;
        request.objectType = object->objectType;
        request.priority = person->priority;
        person->lamportClock += 1;
        printf("\tAFTER_CRITICAL, %d: SEND ACKALL to id: %d about objectId: %d\n", person->id, i, request.objectId);
        MPI_Send(&request, 1, MPI_REQ, i, ACKALL, MPI_COMM_WORLD);
    }
}