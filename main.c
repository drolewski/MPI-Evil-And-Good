#include <unistd.h>
#include <string.h>
#include "structure.h"
#include "functions.h"

const int toiletNumber = 2;
const int potNumber = 1;
const int goodNumber = 3;
const int badNumber = 7;

Person init(int id, Object* toiletList, Object* potList){
    toiletList = malloc(sizeof(struct Object)*(toiletNumber + potNumber));
    potList = malloc(sizeof(struct Object)*(toiletNumber + potNumber));

    for(int i = 0; i < potNumber; i++) {
        Object pot;
        pot.id = i + 1;
        pot.noInList = i;
        pot.ObjectState = ObjectState.repaired;
        potList[i] = pot;
    }

    for(int i = 0; i < toiletNumber; i++) {
        Object toilet;
        toilet.id = i + 1;
        toilet.noInList = i;
        toilet.ObjectState = ObjectState.repaired;
        toiletList[i] = toilet;
    }

    struct Person person;
    person.id = id;
    person.personType = person.id <= goodNumber ? PersonType.good : PersonType.bad; 
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

int main (int argc, char **argv) {

    MPI_Init(int argc, char **argv);

    int size,rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    MPI_Status status;
    if (rank == 0) {
        for(int i = 1; i <= (goodNumber + badNumber); i++){
            MPI_Send(i, 1, MPI_INT, i, SYNCHR, MPI_COMM_WORLD);
        }
        int counter = 0;
        while(counter < (goodNumber + badNumber)) {
            int sourceId;
            MPI_Recv(sourceId, 1, MPI_INT, MPI_ANY_SOURCE, SYNCHR, MPI_COMM_WORLD, &status);
            printf("SYNCHR Message Received from: %d\n", sourceId);
            counter++;
        }
        printf("SYNCHR done!\n");
    } else {
        int id;
        MPI_Recv(id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD, &status);
        struct Object* toiletList;
        struct Object* potList;
        Person person = init(id, toiletList, potList);
        printf("Process: %d is Person: %d, %s\n", rank, person.id, person.personType ? "good" : "bad");
        MPI_Send(id, 1, MPI_INT, 0, SYNCHR, MPI_COMM_WORLD);
    }    

    MPI_Finalize();
}