#!/bin/bash

echo "g++ -o pmi_prog -g -lpmi -L./lib/ pmi_prog.cpp"
g++ -o pmi_prog -g -lpmi -L./lib/ pmi_prog.cpp