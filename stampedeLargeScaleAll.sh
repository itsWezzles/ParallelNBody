#!/bin/bash
#SBATCH --ntasks 4096         #Number of processes
#SBATCH -t 03:00:00                #Runtime in minutes
#SBATCH -p normal   	      #Partition to submit to
#SBATCH -o data/stampedefull512k.out     	      #File to which standard out will be written
#SBATCH -e data/stampedefull512k.err      	      #File to which standard err will be written

module swap intel gcc/4.7.1

make clean

make symmetric XFLAGS='-DP2P_NUM_THREADS=0 -DP2P_DECAY_ITERATOR=0'

#
# Execute the run
#
ibrun -n 1  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 2  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 4  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 4  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 8  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 8  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 16  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 16  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 16  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 32  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 32  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 32  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 64  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 64  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 64  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 64  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 128  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 128  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 128  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 128  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 256  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 256  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 256  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 256  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 256  ./symmetric 512000  -c 16 #-nocheck

ibrun -n 512  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 512  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 512  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 512  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 512  ./symmetric 512000  -c 16 #-nocheck

ibrun -n 1024  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 1024  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 1024  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 1024  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 1024  ./symmetric 512000  -c 16 #-nocheck

ibrun -n 1024  ./symmetric 512000  -c 32 #-nocheck

ibrun -n 2048  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 2048  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 2048  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 2048  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 2048  ./symmetric 512000  -c 16 #-nocheck

ibrun -n 2048  ./symmetric 512000  -c 32 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 1 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 2 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 4 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 8 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 16 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 32 #-nocheck

ibrun -n 4096  ./symmetric 512000  -c 64 #-nocheck