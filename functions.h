#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <time.h>
#include <sys/time.h>
#include "structure.h"

void init(int id, Object* toiletList, Object* potList);

void preparing();

void waitCritical();

void inCritical();

void afterCritical();

void rest();

#endif