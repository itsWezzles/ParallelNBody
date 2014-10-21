#!/bin/bash
#SBATCH -n 64         #Number of processes
#SBATCH -t 00:30:00                #Runtime in minutes
#SBATCH -p normal   	      #Partition to submit to

#SBATCH -o data/test.out     	      #File to which standard out will be written
#SBATCH -e data/test.err      	      #File to which standard err will be written

module swap intel gcc/4.7.1

make clean

make symmetric XFLAGS='-DP2P_NUM_THREADS=0 -DP2P_DECAY_ITERATOR=0'

#
# Execute the run
#

mpirun -np 64  ./symmetric 25600  -c 4 #-nocheck