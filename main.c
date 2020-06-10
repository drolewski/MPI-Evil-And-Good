#include <unistd.h>
#include <string.h>
#include "structure.h"
#include "functions.h"

int main () {

    const int toiletNumber = 2;
    const int potNumber = 1;
    const int goodNumber = 3;
    const int badNumber = 7;
    struct Person* personList;
    struct Object* toiletList;
    struct Object* potList;

    personList = malloc(sizeof(struct Person)*(goodNumber + badNumber));
    toiletList = malloc(sizeof(struct Object)*(toiletNumber + potNumber));
    potList = malloc(sizeof(struct Object)*(toiletNumber + potNumber));

    for(int i = 0; i < potNumber; i++){
        Object pot;
        pot.id = i + 1;
        pot.noInList = i;
        pot.ObjectState = ObjectState.repaired;
        potList[i] = pot;
    }

    for(int i = 0; i < toiletNumber; i++){
        Object toilet;
        toilet.id = i + 1;
        toilet.noInList = i;
        toilet.ObjectState = ObjectState.repaired;
        toiletList[i] = toilet;
    }

    for(int i = 0; i < (goodNumber + badNumber); i++){
        struct Person person;
        person.id = i + 1;
        person.personType = person.id <= goodNumber ? PersonType.good : PersonType.bad; 
        person.goodCount = goodNumber;
        person.badCount = badNumber;
        person.avaliableObjectsCount = toiletNumber + potNumber;
        person.toiletList = toiletList;
        person.potList = potList;
        person.priority = 0;
        person.messageCount = 0; 
        person.lamportClock = 0;
        personList[i] = person;
    }
    
}