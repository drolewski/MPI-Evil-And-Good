#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <time.h>
#include <sys/time.h>
#include "structure.h"

Person init(int id, Object *toiletList, Object *potList);

int preparingRequestHandler(Request *request);

int waitCritical(Object *objectList, int listSize, int *objectId, int *objectType);

void inCritical();

void afterCritical(Object *object);

void rest();

void handleRequests();

void updateLists(Request *request, char* stateName);

void updateLamportClock();

void handleStates();

int preparingState(Object *objectList, int rejectedRest);
#endif