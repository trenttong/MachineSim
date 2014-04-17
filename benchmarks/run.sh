#!/bin/bash

pin  -t cachesim.so -- java  -XX:+UseCodeCacheFlushing -XX:CICompilerCount=1 -XX:+PrintCompilation -jar dacapo.jar  eclipse

#end
