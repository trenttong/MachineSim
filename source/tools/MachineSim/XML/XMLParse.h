/*****************************************************************************
 *                                McPAT
 *                      SOFTWARE LICENSE AGREEMENT
 *            Copyright 2012 Hewlett-Packard Development Company, L.P.
 *                          All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.”
 *
 ***************************************************************************/

#ifndef XML_PARSE_H_
#define XML_PARSE_H_

#include <stdio.h>
#include "XMLParser.h"
#include <string.h>
#include <iostream>
using namespace std;

typedef struct {
  int cache_enable;
  int cache_linesize;
  int number_entries;
  int associativity;
} cache_systemcore;

typedef struct{
   cache_systemcore LM_itlb;
   cache_systemcore LM_dtlb;
   cache_systemcore L1_itlb;
   cache_systemcore L1_dtlb;
   cache_systemcore L2_utlb;
   cache_systemcore L1_icache;
   cache_systemcore L1_dcache;
   cache_systemcore L2_ucache;
   cache_systemcore L3_ucache;
}  root_system;

class ParseXML
{
private:
    void parse_cache_params(const XMLNode &xNode, cache_systemcore *cache);
    void print_cache_params(FILE *out, const char *cache_name, cache_systemcore* cache);
public:
    void parse(const char* filepath);
    void initialize();
    void print(FILE *out);
public:
    root_system sys;
};

#endif /* XML_PARSE_H_ */
