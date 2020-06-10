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
    person.avaliableObjectsCount = toiletNumber + potNumber;
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
    int nItems = 4;
    int blockLengths[4] = {1, 1, 1, 1};
    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT};

    MPI_Aint offsets[4];
    offsets[0] = offsetof(Request, id);
    offsets[1] = offsetof(Request, requestType);
    offsets[2] = offsetof(Request, objectId);
    offsets[3] = offsetof(Request, priority);

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
        printf("Process: %d is Person: %d, %s\n", rank, person.id, person.personType - 100 ? "good" : "bad");
        MPI_Send(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD);

        MPI_Recv(&id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD, &status);

        waitRandomTime(id);

        preparing(&person, toiletList, potList);
        waitCritical(&person, toiletList, potList);
    }

    MPI_Finalize();
}

void preparing(Person *person, Object *toiletList, Object *potList)
{
    //            MPI_Send(&i, 1, MPI_INT, i, SYNCHR, MPI_COMM_WORLD);

    while (true)
    {
        // weź sprawdzaj wiadomosci przychodzące
        MPI_Status status;
        Request request;
        MPI_IRecv(&request, sizeof(Request), MPI_Request, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_ERROR == MPI_SUCCESS)
        {
            switch (request.requestType)
            {
            case PREQ:
                break;
            case TREQ:
                break;
            case PACK:
                break;
            case TACK:
                break;
            case REJECT:
                break;
            case ACKALL:
                break;
            default:
                break;
            }
        }
        // sprawdzaj czy mozesz cos zrobic
        if (true)
        {
            break;
        }
    }
    // wysylamy do wszystkich odpowiedni req
    for (int i = 1; i <= (goodNumber + badNumber); i++)
    {
        MPI_Send(&i, 1, MPI_INT, i, SYNCHR, MPI_COMM_WORLD);
    }
}