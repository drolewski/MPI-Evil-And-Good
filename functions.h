#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <time.h>
#include <sys/time.h>
#include "structure.h"

Person init(int id, Object *toiletList, Object *potList);

int preparing(Person *person, Object *objectList);

int waitCritical(Person *person, Object *objectList, int listSize, int *objectId, int *objectType);

void inCritical(Person *person);

void afterCritical(Person *person, Object *object);

void rest(Person *person);

#endif