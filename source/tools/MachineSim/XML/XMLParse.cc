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

#include <stdio.h>
#include "XMLParser.h"
#include <string>
#include "XMLParse.h"
#include <iostream>

using namespace std;

#define MY_FPRINTF fprintf
#define PARSEXML_PRINT_FIELD(O,N,S,X)  MY_FPRINTF(O, "%s.%s:%d\n", N, S, X);

#define PARSE_CHILD_PARAMS(X, Y)                                      do {  \
  unsigned int NumofCom = xNode.nChildNode("param");                        \
  for (unsigned int k=0; k <NumofCom; ++k)                                  \
  {                                                                         \
    if (!strcmp(xNode.getChildNode("param",k).getAttribute("name"),X))      \
         Y=atoi(xNode.getChildNode("param",k).getAttribute("value"));       \
  }                                                                         \
} while(0);

void ParseXML::parse_cache_params(const XMLNode &xNode, cache_systemcore *cache)
{
   PARSE_CHILD_PARAMS("cache_enable"  , cache->cache_enable); 
   PARSE_CHILD_PARAMS("number_entries", cache->number_entries); 
   PARSE_CHILD_PARAMS("cache_linesize", cache->cache_linesize); 
   PARSE_CHILD_PARAMS("associativity" , cache->associativity); 
}

void ParseXML::parse(const char* filepath)
{
   //Initialize all structures
   ParseXML::initialize();

   // this open and parse the XML file:
   XMLNode xMainNode=XMLNode::openFileHelper(filepath,"component"); //the 'component' in the first layer
   XMLNode xNode1=xMainNode.getChildNode("component"); // the 'component' in the second layer

   unsigned int NumofCom_2=xNode1.nChildNode("component");
   for (unsigned int i=0; i<NumofCom_2; i++)
   {
      XMLNode xNode2=xNode1.getChildNode("component",i);
      if (!strcmp(xNode2.getAttribute("id"), "system.LM_itlb"))   parse_cache_params(xNode2, &sys.LM_itlb);
      if (!strcmp(xNode2.getAttribute("id"), "system.LM_dtlb"))   parse_cache_params(xNode2, &sys.LM_dtlb);
      if (!strcmp(xNode2.getAttribute("id"), "system.L1_itlb"))   parse_cache_params(xNode2, &sys.L1_itlb); 
      if (!strcmp(xNode2.getAttribute("id"), "system.L1_dtlb"))   parse_cache_params(xNode2, &sys.L1_dtlb); 
      if (!strcmp(xNode2.getAttribute("id"), "system.L2_utlb"))   parse_cache_params(xNode2, &sys.L2_utlb); 
      if (!strcmp(xNode2.getAttribute("id"), "system.L1_icache")) parse_cache_params(xNode2, &sys.L1_icache); 
      if (!strcmp(xNode2.getAttribute("id"), "system.L1_dcache")) parse_cache_params(xNode2, &sys.L1_dcache); 
      if (!strcmp(xNode2.getAttribute("id"), "system.L2_ucache")) parse_cache_params(xNode2, &sys.L2_ucache); 
      if (!strcmp(xNode2.getAttribute("id"), "system.L3_ucache")) parse_cache_params(xNode2, &sys.L3_ucache); 
   }

   return;
}

void ParseXML::initialize() //Initialize all
{
   memset(&sys, 0, sizeof(root_system));
}

void ParseXML::print_cache_params(FILE *out, const char *cache_name, cache_systemcore* cache) 
{
   PARSEXML_PRINT_FIELD(out, cache_name, "cache_enable"  , cache->cache_enable);
   PARSEXML_PRINT_FIELD(out, cache_name, "number_entries", cache->number_entries);
   PARSEXML_PRINT_FIELD(out, cache_name, "cache_linesize", cache->cache_linesize);
   PARSEXML_PRINT_FIELD(out, cache_name, "associativity" , cache->associativity);
}

void ParseXML::print(FILE *out)
{
   print_cache_params(out, "LM_itlb"  , &sys.LM_itlb);
   print_cache_params(out, "LM_dtlb"  , &sys.LM_dtlb);
   print_cache_params(out, "L1_itlb"  , &sys.L1_itlb);
   print_cache_params(out, "L1_dtlb"  , &sys.L1_dtlb);
   print_cache_params(out, "L2_utlb"  , &sys.L2_utlb);
   print_cache_params(out, "L1_icache", &sys.L1_icache);
   print_cache_params(out, "L1_dcache", &sys.L1_dcache);
   print_cache_params(out, "L2_ucache", &sys.L2_ucache);
   print_cache_params(out, "L3_ucache", &sys.L3_ucache);
}
