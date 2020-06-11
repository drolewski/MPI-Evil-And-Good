#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <time.h>
#include <sys/time.h>
#include "structure.h"

Person init(int id, Object* toiletList, Object* potList);

int preparing(Person* person, Object* objectList);

void waitCritical(Person* person, Object* objectList, int listSize);

void inCritical();

void afterCritical();

void rest();

#endif