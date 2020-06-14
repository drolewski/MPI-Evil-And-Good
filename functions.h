#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <time.h>
#include <sys/time.h>
#include "structure.h"

Person init(int id, Object *toiletList, Object *potList);

void preparingRequestHandler(Request *request);

void waitCriticalRequestHandler(Request *request, Object *objectList);

void inCriticalState();

void afterCriticalState(Object *object);

void restRequestHandler(Request *request);

void* handleRequests();

void updateLists(Request *request, char* stateName);

void updateLamportClock();

void handleStates();

int preparingState(Object *objectList, int rejectedRest);

int waitCriticalState(int *objectId, int *objectType);

void sendRequestForObjects(Object *ObjectList, int iterator, int rejectedRest);
#endif