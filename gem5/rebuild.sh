#!/bin/bash

printf "\n=== Removing previous build ===\n"
rm -rf build/X86

printf "\n=== Building gem5.opt ===\n"
printf "\ny\n" | scons build/X86/gem5.opt -j$(nproc) PROTOCOL=MESI_Three_Level