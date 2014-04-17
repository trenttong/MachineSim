#!/bin/bash
objdump -d -M intel $1 &> $1.asm
#end
