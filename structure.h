#ifndef STRUCTURE_H
#define STRUCTURE_H

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define false 0
#define true 1

#define PREQ 10
#define PACK 20
#define TREQ 30
#define TACK 40
#define SYNCHR 50
#define ACKALL 60
#define REJECT 70

enum PersonType {
    bad,
    good    
}; typedef enum PersonType PersonType;

enum ObjectType {
    pot,
    toilet
}; typedef enum ObjectType ObjectType;

enum ObjectState {
    broken,
    repaired
}; typedef enum ObjectState ObjectState;

struct Person {
    PersonType personType;
    int id;
    int goodCount;
    int badCount;
    Object *potList;
    Object *toiletList;
    int avaliableObjectsCount;
    int messageCount;
    int lamportClock;
    int priority;
}; typedef struct Person Person;

struct Object {
    ObjectType objectType;
    int id;
    ObjectState ObjectState;
    int noInList;
}; typedef struct Object Object;

struct Request {
    int id;
    int requestType;
    int objectId;
    int priority;
}; typedef struct Request Request;


struct ARequest {
    int id;
    int requestType;
    int objectId;
    int priority;
    int objectState;
    int objectType;
}; typedef struct ARequest ARequest;

#endif