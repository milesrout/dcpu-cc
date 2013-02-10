/**
 * 
 *    File:       aerr.h
 * 
 *    Project:    DCPU-16 Tools
 *    Component:  Assembler
 * 
 *    Authors:    James Rhodes
 * 
 *    Description:    Declares error codes and functions in the
 *            error handling infrastructure.
 * 
 **/

#ifndef __DCPUCC_AERR_H
#define __DCPUCC_AERR_H

#include <stdint.h>
#include <stdlib.h>

// Structure for holding warning policy information
struct dcpucc_warnpolicy
{
    int errid;
    const char* name;
    bool treat_as_error; //< Whether the warning should be treated as an error.
    bool silence; //< Whether the warning should be silenced.
};

// Error codes
#include "derr.defs.h"


#endif
