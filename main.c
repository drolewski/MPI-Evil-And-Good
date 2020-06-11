#include <unistd.h>
#include <string.h>
#include "structure.h"
#include "functions.h"

const int toiletNumber = 2;
const int potNumber = 1;
const int goodNumber = 3;
const int badNumber = 7;

MPI_Datatype MPI_Request;
MPI_Datatype MPI_ARequest;

Person init(int id, Object *toiletList, Object *potList)
{
    toiletList = malloc(sizeof(struct Object) * (toiletNumber + potNumber));
    potList = malloc(sizeof(struct Object) * (toiletNumber + potNumber));

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

    MPI_Type_create_struct(nItems, blockLengths, offsets, types, &MPI_Request);
    MPI_Type_commit(&MPI_Request);

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
    setupStructures();
    
    MPI_Init(&argc, &argv);

    int size, rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

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

        preparing(&person);
        waitCritical(&person);
    }

    MPI_Finalize();
}

void preparing(Person *person)
{
    while(true){
        if(person->avaliableObjectsCount > 0){
            if(person->personType - 100)
            {
                // good
                for(int i = 0; i < toiletNumber; i++)
                {
                    if(person->toiletList[i].objectState == BROKEN)
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
                        for(int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if(i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_Request, i, TREQ, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
                for(int i = 0; i < potNumber; i++)
                {
                    if(person->potList[i].objectState == BROKEN)
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
                        for(int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if(i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_Request, i, PREQ, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
                person->lamportClock += 1;
            }
            else
            {
                // bad
                for(int i = 0; i < toiletNumber; i++)
                {
                    if(person->toiletList[i].objectState == REPAIRED)
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
                        for(int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if(i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_Request, i, TREQ, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
                for(int i = 0; i < potNumber; i++)
                {
                    if(person->potList[i].objectState == REPAIRED)
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
                        for(int i = 1; i <= (person->goodCount + person->badCount); i++)
                        {
                            if(i != person->id)
                            {
                                MPI_Send(&req, 1, MPI_Request, i, PREQ, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
                person->lamportClock += 1;
            }    
            break;    
        }
        else
        {
            MPI_Status status;
            Request request;
            MPI_Recv(&request, sizeof(Request), MPI_Request, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            if (status.MPI_ERROR == MPI_SUCCESS)
            {
                int receivedId = request.id;
                switch (request.requestType)
                {
                case PREQ:
                    request.id = person->id;
                    request.requestType = PACK;
                    printf("\tSend PACK to: %d\n", receivedId);
                    MPI_Send(&request, 1, MPI_Request, receivedId, PACK, MPI_COMM_WORLD);
                    break;
                case TREQ:
                    request.id = person->id;
                    request.requestType = TACK;
                    printf("\tSend TACK to: %d\n", receivedId);
                    MPI_Send(&request, 1, MPI_Request, receivedId, TACK, MPI_COMM_WORLD);
                    break;
                case ACKALL:
                    if(request.objectType == POT)
                    {
                        printf("\tReceive ACK_ALL with pot: %d and state: %s\n", receivedId, request.objectState - BROKEN ? "repaired": "broken");
                        person->potList[request.objectId - 1].objectState = request.objectState;
                        if(person->personType == GOOD)
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
                        printf("\tReceive ACK_ALL with toilet: %d and state: %s\n", receivedId, request.objectState - BROKEN ? "repaired": "broken");
                        person->toiletList[request.objectId - 1].objectState = request.objectState;
                        if(person->personType == GOOD)
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
                    printf("Received ignore message.\n");
                    break;
                }
            }
        }
    }
}