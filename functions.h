#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <time.h>
#include <sys/time.h>
#include "structure.h"

Person init(int id, Object* toiletList, Object* potList);

void preparing(Person* person, Object* toiletList, Object* potList);

void waitCritical(Person* person, Object* toiletList, Object* potList);

void inCritical();

void afterCritical();

void rest();

#endif