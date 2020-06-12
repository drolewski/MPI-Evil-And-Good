#ifndef STRUCTURE_H
#define STRUCTURE_H

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>

#define false 0
#define true 1

#define PREQ 10
#define PACK 20
#define TREQ 30
#define TACK 40
#define SYNCHR 50
#define ACKALL 60
#define REJECT 70

#define INIT 410
#define PREPARING 420
#define WAIT_CRITICAL 430
#define IN_CRITICAL 440
#define AFTER_CRITICAL 450
#define REST 460

#define BAD 100
#define GOOD 101

#define POT 200
#define TOILET 201

#define BROKEN 300
#define REPAIRED 301


#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

struct Object
{
    int objectType;
    int id;
    int objectState;
    int noInList;
};
typedef struct Object Object;

struct Person
{
    int personType;
    int id;
    int goodCount;
    int badCount;
    Object *potList;
    Object *toiletList;
    int avaliableObjectsCount;
    int messageCount;
    int lamportClock;
    int priority;
};
typedef struct Person Person;

 struct Request
{
    int id;
    int requestType;
    int objectId;
    int priority;
    int objectState;
    int objectType;
};

typedef struct Request Request;

extern MPI_Datatype MPI_REQ;

#endif